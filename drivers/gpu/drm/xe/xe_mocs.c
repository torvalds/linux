// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_mocs.h"

#include "regs/xe_gt_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_engine.h"
#include "xe_gt.h"
#include "xe_mmio.h"
#include "xe_platform_types.h"
#include "xe_step_types.h"

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
#define mocs_dbg drm_dbg
#else
__printf(2, 3)
static inline void mocs_dbg(const struct drm_device *dev,
			    const char *format, ...)
{ /* noop */ }
#endif

/*
 * MOCS indexes used for GPU surfaces, defining the cacheability of the
 * surface data and the coherency for this data wrt. CPU vs. GPU accesses.
 */
enum xe_mocs_info_index {
	/*
	 * Not cached anywhere, coherency between CPU and GPU accesses is
	 * guaranteed.
	 */
	XE_MOCS_UNCACHED,
	/*
	 * Cacheability and coherency controlled by the kernel automatically
	 * based on the xxxx  IOCTL setting and the current
	 * usage of the surface (used for display scanout or not).
	 */
	XE_MOCS_PTE,
	/*
	 * Cached in all GPU caches available on the platform.
	 * Coherency between CPU and GPU accesses to the surface is not
	 * guaranteed without extra synchronization.
	 */
	XE_MOCS_CACHED,
};

enum {
	HAS_GLOBAL_MOCS = BIT(0),
	HAS_RENDER_L3CC = BIT(1),
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

/* Helper defines */
#define GEN9_NUM_MOCS_ENTRIES	64  /* 63-64 are reserved, but configured. */
#define PVC_NUM_MOCS_ENTRIES	3
#define MTL_NUM_MOCS_ENTRIES    16

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
 * that, for Icelake and above, list of entries is published as part
 * of bspec.
 *
 * Entries not part of the following tables are undefined as far as
 * userspace is concerned and shouldn't be relied upon.  For Gen < 12
 * they will be initialized to PTE. Gen >= 12 don't have a setting for
 * PTE and those platforms except TGL/RKL will be initialized L3 WB to
 * catch accidental use of reserved and unused mocs indexes.
 *
 * The last few entries are reserved by the hardware. For ICL+ they
 * should be initialized according to bspec and never used, for older
 * platforms they should never be written to.
 *
 * NOTE1: These tables are part of bspec and defined as part of hardware
 *       interface for ICL+. For older platforms, they are part of kernel
 *       ABI. It is expected that, for specific hardware platform, existing
 *       entries will remain constant and the table will only be updated by
 *       adding new entries, filling unused positions.
 *
 * NOTE2: For GEN >= 12 except TGL and RKL, reserved and unspecified MOCS
 *       indices have been set to L3 WB. These reserved entries should never
 *       be used, they may be changed to low performant variants with better
 *       coherency in the future if more entries are needed.
 *       For TGL/RKL, all the unspecified MOCS indexes are mapped to L3 UC.
 */

#define GEN11_MOCS_ENTRIES \
	/* Entries 0 and 1 are defined per-platform */ \
	/* Base - L3 + LLC */ \
	MOCS_ENTRY(2, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), \
		L3_3_WB), \
	/* Base - Uncached */ \
	MOCS_ENTRY(3, \
		LE_1_UC | LE_TC_1_LLC, \
		L3_1_UC), \
	/* Base - L3 */ \
	MOCS_ENTRY(4, \
		LE_1_UC | LE_TC_1_LLC, \
		L3_3_WB), \
	/* Base - LLC */ \
	MOCS_ENTRY(5, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), \
		L3_1_UC), \
	/* Age 0 - LLC */ \
	MOCS_ENTRY(6, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(1), \
		L3_1_UC), \
	/* Age 0 - L3 + LLC */ \
	MOCS_ENTRY(7, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(1), \
		L3_3_WB), \
	/* Age: Don't Chg. - LLC */ \
	MOCS_ENTRY(8, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(2), \
		L3_1_UC), \
	/* Age: Don't Chg. - L3 + LLC */ \
	MOCS_ENTRY(9, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(2), \
		L3_3_WB), \
	/* No AOM - LLC */ \
	MOCS_ENTRY(10, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_AOM(1), \
		L3_1_UC), \
	/* No AOM - L3 + LLC */ \
	MOCS_ENTRY(11, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_AOM(1), \
		L3_3_WB), \
	/* No AOM; Age 0 - LLC */ \
	MOCS_ENTRY(12, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(1) | LE_AOM(1), \
		L3_1_UC), \
	/* No AOM; Age 0 - L3 + LLC */ \
	MOCS_ENTRY(13, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(1) | LE_AOM(1), \
		L3_3_WB), \
	/* No AOM; Age:DC - LLC */ \
	MOCS_ENTRY(14, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(2) | LE_AOM(1), \
		L3_1_UC), \
	/* No AOM; Age:DC - L3 + LLC */ \
	MOCS_ENTRY(15, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(2) | LE_AOM(1), \
		L3_3_WB), \
	/* Self-Snoop - L3 + LLC */ \
	MOCS_ENTRY(18, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SSE(3), \
		L3_3_WB), \
	/* Skip Caching - L3 + LLC(12.5%) */ \
	MOCS_ENTRY(19, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(7), \
		L3_3_WB), \
	/* Skip Caching - L3 + LLC(25%) */ \
	MOCS_ENTRY(20, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(3), \
		L3_3_WB), \
	/* Skip Caching - L3 + LLC(50%) */ \
	MOCS_ENTRY(21, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(1), \
		L3_3_WB), \
	/* Skip Caching - L3 + LLC(75%) */ \
	MOCS_ENTRY(22, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_RSC(1) | LE_SCC(3), \
		L3_3_WB), \
	/* Skip Caching - L3 + LLC(87.5%) */ \
	MOCS_ENTRY(23, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_RSC(1) | LE_SCC(7), \
		L3_3_WB), \
	/* HW Reserved - SW program but never use */ \
	MOCS_ENTRY(62, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), \
		L3_1_UC), \
	/* HW Reserved - SW program but never use */ \
	MOCS_ENTRY(63, \
		LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), \
		L3_1_UC)

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

static const struct xe_mocs_entry gen12_mocs_desc[] = {
	GEN11_MOCS_ENTRIES,
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

static const struct xe_mocs_entry dg2_mocs_desc_g10_ax[] = {
	/* Wa_14011441408: Set Go to Memory for MOCS#0 */
	MOCS_ENTRY(0, 0, L3_1_UC | L3_GLBGO(1) | L3_LKUP(1)),
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

static unsigned int get_mocs_settings(struct xe_device *xe,
				      struct xe_mocs_info *info)
{
	unsigned int flags;

	memset(info, 0, sizeof(struct xe_mocs_info));

	info->unused_entries_index = XE_MOCS_PTE;
	switch (xe->info.platform) {
	case XE_PVC:
		info->size = ARRAY_SIZE(pvc_mocs_desc);
		info->table = pvc_mocs_desc;
		info->n_entries = PVC_NUM_MOCS_ENTRIES;
		info->uc_index = 1;
		info->wb_index = 2;
		info->unused_entries_index = 2;
		break;
	case XE_METEORLAKE:
		info->size = ARRAY_SIZE(dg2_mocs_desc);
		info->table = dg2_mocs_desc;
		info->n_entries = MTL_NUM_MOCS_ENTRIES;
		info->uc_index = 1;
		info->unused_entries_index = 3;
		break;
	case XE_DG2:
		if (xe->info.subplatform == XE_SUBPLATFORM_DG2_G10 &&
		    xe->info.step.graphics >= STEP_A0 &&
		    xe->info.step.graphics <= STEP_B0) {
			info->size = ARRAY_SIZE(dg2_mocs_desc_g10_ax);
			info->table = dg2_mocs_desc_g10_ax;
		} else {
			info->size = ARRAY_SIZE(dg2_mocs_desc);
			info->table = dg2_mocs_desc;
		}
		info->uc_index = 1;
		info->n_entries = GEN9_NUM_MOCS_ENTRIES;
		info->unused_entries_index = 3;
		break;
	case XE_DG1:
		info->size = ARRAY_SIZE(dg1_mocs_desc);
		info->table = dg1_mocs_desc;
		info->uc_index = 1;
		info->n_entries = GEN9_NUM_MOCS_ENTRIES;
		info->uc_index = 1;
		info->unused_entries_index = 5;
		break;
	case XE_TIGERLAKE:
	case XE_ALDERLAKE_S:
	case XE_ALDERLAKE_P:
		info->size  = ARRAY_SIZE(gen12_mocs_desc);
		info->table = gen12_mocs_desc;
		info->n_entries = GEN9_NUM_MOCS_ENTRIES;
		info->uc_index = 3;
		info->unused_entries_index = 2;
		break;
	default:
		drm_err(&xe->drm, "Platform that should have a MOCS table does not.\n");
		return 0;
	}

	if (XE_WARN_ON(info->size > info->n_entries))
		return 0;

	flags = HAS_RENDER_L3CC;
	if (!IS_DGFX(xe))
		flags |= HAS_GLOBAL_MOCS;

	return flags;
}

/*
 * Get control_value from MOCS entry taking into account when it's not used
 * then if unused_entries_index is non-zero then its value will be returned
 * otherwise XE_MOCS_PTE's value is returned in this case.
 */
static u32 get_entry_control(const struct xe_mocs_info *info,
			     unsigned int index)
{
	if (index < info->size && info->table[index].used)
		return info->table[index].control_value;
	return info->table[info->unused_entries_index].control_value;
}

static void __init_mocs_table(struct xe_gt *gt,
			      const struct xe_mocs_info *info,
			      u32 addr)
{
	struct xe_device *xe = gt_to_xe(gt);

	unsigned int i;
	u32 mocs;

	mocs_dbg(&gt->xe->drm, "entries:%d\n", info->n_entries);
	drm_WARN_ONCE(&xe->drm, !info->unused_entries_index,
		      "Unused entries index should have been defined\n");
	for (i = 0;
	     i < info->n_entries ? (mocs = get_entry_control(info, i)), 1 : 0;
	     i++) {
		mocs_dbg(&gt->xe->drm, "%d 0x%x 0x%x\n", i, _MMIO(addr + i * 4).reg, mocs);
		xe_mmio_write32(gt, _MMIO(addr + i * 4).reg, mocs);
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

	mocs_dbg(&gt->xe->drm, "entries:%d\n", info->n_entries);
	for (i = 0;
	     i < (info->n_entries + 1) / 2 ?
	     (l3cc = l3cc_combine(get_entry_l3cc(info, 2 * i),
				  get_entry_l3cc(info, 2 * i + 1))), 1 : 0;
	     i++) {
		mocs_dbg(&gt->xe->drm, "%d 0x%x 0x%x\n", i, GEN9_LNCFCMOCS(i).reg, l3cc);
		xe_mmio_write32(gt, GEN9_LNCFCMOCS(i).reg, l3cc);
	}
}

void xe_mocs_init_engine(const struct xe_engine *engine)
{
	struct xe_mocs_info table;
	unsigned int flags;

	flags = get_mocs_settings(engine->gt->xe, &table);
	if (!flags)
		return;

	if (flags & HAS_RENDER_L3CC && engine->class == XE_ENGINE_CLASS_RENDER)
		init_l3cc_table(engine->gt, &table);
}

void xe_mocs_init(struct xe_gt *gt)
{
	struct xe_mocs_info table;
	unsigned int flags;

	/*
	 * LLC and eDRAM control values are not applicable to dgfx
	 */
	flags = get_mocs_settings(gt->xe, &table);
	mocs_dbg(&gt->xe->drm, "flag:0x%x\n", flags);
	gt->mocs.uc_index = table.uc_index;
	gt->mocs.wb_index = table.wb_index;

	if (flags & HAS_GLOBAL_MOCS)
		__init_mocs_table(gt, &table, GEN12_GLOBAL_MOCS(0).reg);

	/*
	 * Initialize the L3CC table as part of mocs initalization to make
	 * sure the LNCFCMOCSx registers are programmed for the subsequent
	 * memory transactions including guc transactions
	 */
	if (flags & HAS_RENDER_L3CC)
		init_l3cc_table(gt, &table);
}

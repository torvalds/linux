/*
 * Copyright (c) 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions: *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "i915_drv.h"

#include "intel_engine.h"
#include "intel_gt.h"
#include "intel_mocs.h"
#include "intel_lrc.h"
#include "intel_ring.h"

/* structures required */
struct drm_i915_mocs_entry {
	u32 control_value;
	u16 l3cc_value;
	u16 used;
};

struct drm_i915_mocs_table {
	unsigned int size;
	unsigned int n_entries;
	const struct drm_i915_mocs_entry *table;
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

/* Helper defines */
#define GEN9_NUM_MOCS_ENTRIES	62  /* 62 out of 64 - 63 & 64 are reserved. */
#define GEN11_NUM_MOCS_ENTRIES	64  /* 63-64 are reserved, but configured. */

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
 * they will be initialized to PTE. Gen >= 12 onwards don't have a setting for
 * PTE and will be initialized to an invalid value.
 *
 * The last two entries are reserved by the hardware. For ICL+ they
 * should be initialized according to bspec and never used, for older
 * platforms they should never be written to.
 *
 * NOTE: These tables are part of bspec and defined as part of hardware
 *       interface for ICL+. For older platforms, they are part of kernel
 *       ABI. It is expected that, for specific hardware platform, existing
 *       entries will remain constant and the table will only be updated by
 *       adding new entries, filling unused positions.
 */
#define GEN9_MOCS_ENTRIES \
	MOCS_ENTRY(I915_MOCS_UNCACHED, \
		   LE_1_UC | LE_TC_2_LLC_ELLC, \
		   L3_1_UC), \
	MOCS_ENTRY(I915_MOCS_PTE, \
		   LE_0_PAGETABLE | LE_TC_2_LLC_ELLC | LE_LRUM(3), \
		   L3_3_WB)

static const struct drm_i915_mocs_entry skl_mocs_table[] = {
	GEN9_MOCS_ENTRIES,
	MOCS_ENTRY(I915_MOCS_CACHED,
		   LE_3_WB | LE_TC_2_LLC_ELLC | LE_LRUM(3),
		   L3_3_WB)
};

/* NOTE: the LE_TGT_CACHE is not used on Broxton */
static const struct drm_i915_mocs_entry broxton_mocs_table[] = {
	GEN9_MOCS_ENTRIES,
	MOCS_ENTRY(I915_MOCS_CACHED,
		   LE_1_UC | LE_TC_2_LLC_ELLC | LE_LRUM(3),
		   L3_3_WB)
};

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

static const struct drm_i915_mocs_entry tgl_mocs_table[] = {
	/* Base - Error (Reserved for Non-Use) */
	MOCS_ENTRY(0, 0x0, 0x0),
	/* Base - Reserved */
	MOCS_ENTRY(1, 0x0, 0x0),

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

static const struct drm_i915_mocs_entry icl_mocs_table[] = {
	/* Base - Uncached (Deprecated) */
	MOCS_ENTRY(I915_MOCS_UNCACHED,
		   LE_1_UC | LE_TC_1_LLC,
		   L3_1_UC),
	/* Base - L3 + LeCC:PAT (Deprecated) */
	MOCS_ENTRY(I915_MOCS_PTE,
		   LE_0_PAGETABLE | LE_TC_1_LLC,
		   L3_3_WB),

	GEN11_MOCS_ENTRIES
};

enum {
	HAS_GLOBAL_MOCS = BIT(0),
	HAS_ENGINE_MOCS = BIT(1),
	HAS_RENDER_L3CC = BIT(2),
};

static bool has_l3cc(const struct drm_i915_private *i915)
{
	return true;
}

static bool has_global_mocs(const struct drm_i915_private *i915)
{
	return HAS_GLOBAL_MOCS_REGISTERS(i915);
}

static bool has_mocs(const struct drm_i915_private *i915)
{
	return !IS_DGFX(i915);
}

static unsigned int get_mocs_settings(const struct drm_i915_private *i915,
				      struct drm_i915_mocs_table *table)
{
	unsigned int flags;

	if (INTEL_GEN(i915) >= 12) {
		table->size  = ARRAY_SIZE(tgl_mocs_table);
		table->table = tgl_mocs_table;
		table->n_entries = GEN11_NUM_MOCS_ENTRIES;
	} else if (IS_GEN(i915, 11)) {
		table->size  = ARRAY_SIZE(icl_mocs_table);
		table->table = icl_mocs_table;
		table->n_entries = GEN11_NUM_MOCS_ENTRIES;
	} else if (IS_GEN9_BC(i915) || IS_CANNONLAKE(i915)) {
		table->size  = ARRAY_SIZE(skl_mocs_table);
		table->n_entries = GEN9_NUM_MOCS_ENTRIES;
		table->table = skl_mocs_table;
	} else if (IS_GEN9_LP(i915)) {
		table->size  = ARRAY_SIZE(broxton_mocs_table);
		table->n_entries = GEN9_NUM_MOCS_ENTRIES;
		table->table = broxton_mocs_table;
	} else {
		drm_WARN_ONCE(&i915->drm, INTEL_GEN(i915) >= 9,
			      "Platform that should have a MOCS table does not.\n");
		return 0;
	}

	if (GEM_DEBUG_WARN_ON(table->size > table->n_entries))
		return 0;

	/* WaDisableSkipCaching:skl,bxt,kbl,glk */
	if (IS_GEN(i915, 9)) {
		int i;

		for (i = 0; i < table->size; i++)
			if (GEM_DEBUG_WARN_ON(table->table[i].l3cc_value &
					      (L3_ESC(1) | L3_SCC(0x7))))
				return 0;
	}

	flags = 0;
	if (has_mocs(i915)) {
		if (has_global_mocs(i915))
			flags |= HAS_GLOBAL_MOCS;
		else
			flags |= HAS_ENGINE_MOCS;
	}
	if (has_l3cc(i915))
		flags |= HAS_RENDER_L3CC;

	return flags;
}

/*
 * Get control_value from MOCS entry taking into account when it's not used:
 * I915_MOCS_PTE's value is returned in this case.
 */
static u32 get_entry_control(const struct drm_i915_mocs_table *table,
			     unsigned int index)
{
	if (index < table->size && table->table[index].used)
		return table->table[index].control_value;

	return table->table[I915_MOCS_PTE].control_value;
}

#define for_each_mocs(mocs, t, i) \
	for (i = 0; \
	     i < (t)->n_entries ? (mocs = get_entry_control((t), i)), 1 : 0;\
	     i++)

static void __init_mocs_table(struct intel_uncore *uncore,
			      const struct drm_i915_mocs_table *table,
			      u32 addr)
{
	unsigned int i;
	u32 mocs;

	for_each_mocs(mocs, table, i)
		intel_uncore_write_fw(uncore, _MMIO(addr + i * 4), mocs);
}

static u32 mocs_offset(const struct intel_engine_cs *engine)
{
	static const u32 offset[] = {
		[RCS0]  =  __GEN9_RCS0_MOCS0,
		[VCS0]  =  __GEN9_VCS0_MOCS0,
		[VCS1]  =  __GEN9_VCS1_MOCS0,
		[VECS0] =  __GEN9_VECS0_MOCS0,
		[BCS0]  =  __GEN9_BCS0_MOCS0,
		[VCS2]  = __GEN11_VCS2_MOCS0,
	};

	GEM_BUG_ON(engine->id >= ARRAY_SIZE(offset));
	return offset[engine->id];
}

static void init_mocs_table(struct intel_engine_cs *engine,
			    const struct drm_i915_mocs_table *table)
{
	__init_mocs_table(engine->uncore, table, mocs_offset(engine));
}

/*
 * Get l3cc_value from MOCS entry taking into account when it's not used:
 * I915_MOCS_PTE's value is returned in this case.
 */
static u16 get_entry_l3cc(const struct drm_i915_mocs_table *table,
			  unsigned int index)
{
	if (index < table->size && table->table[index].used)
		return table->table[index].l3cc_value;

	return table->table[I915_MOCS_PTE].l3cc_value;
}

static inline u32 l3cc_combine(u16 low, u16 high)
{
	return low | (u32)high << 16;
}

#define for_each_l3cc(l3cc, t, i) \
	for (i = 0; \
	     i < ((t)->n_entries + 1) / 2 ? \
	     (l3cc = l3cc_combine(get_entry_l3cc((t), 2 * i), \
				  get_entry_l3cc((t), 2 * i + 1))), 1 : \
	     0; \
	     i++)

static void init_l3cc_table(struct intel_engine_cs *engine,
			    const struct drm_i915_mocs_table *table)
{
	struct intel_uncore *uncore = engine->uncore;
	unsigned int i;
	u32 l3cc;

	for_each_l3cc(l3cc, table, i)
		intel_uncore_write_fw(uncore, GEN9_LNCFCMOCS(i), l3cc);
}

void intel_mocs_init_engine(struct intel_engine_cs *engine)
{
	struct drm_i915_mocs_table table;
	unsigned int flags;

	/* Called under a blanket forcewake */
	assert_forcewakes_active(engine->uncore, FORCEWAKE_ALL);

	flags = get_mocs_settings(engine->i915, &table);
	if (!flags)
		return;

	/* Platforms with global MOCS do not need per-engine initialization. */
	if (flags & HAS_ENGINE_MOCS)
		init_mocs_table(engine, &table);

	if (flags & HAS_RENDER_L3CC && engine->class == RENDER_CLASS)
		init_l3cc_table(engine, &table);
}

static u32 global_mocs_offset(void)
{
	return i915_mmio_reg_offset(GEN12_GLOBAL_MOCS(0));
}

void intel_mocs_init(struct intel_gt *gt)
{
	struct drm_i915_mocs_table table;
	unsigned int flags;

	/*
	 * LLC and eDRAM control values are not applicable to dgfx
	 */
	flags = get_mocs_settings(gt->i915, &table);
	if (flags & HAS_GLOBAL_MOCS)
		__init_mocs_table(gt->uncore, &table, global_mocs_offset());
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_mocs.c"
#endif

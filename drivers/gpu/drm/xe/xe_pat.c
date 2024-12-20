// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_pat.h"

#include <uapi/drm/xe_drm.h>

#include <generated/xe_wa_oob.h>

#include "regs/xe_reg_defs.h"
#include "xe_assert.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_mmio.h"
#include "xe_sriov.h"
#include "xe_wa.h"

#define _PAT_ATS				0x47fc
#define _PAT_INDEX(index)			_PICK_EVEN_2RANGES(index, 8, \
								   0x4800, 0x4804, \
								   0x4848, 0x484c)
#define _PAT_PTA				0x4820

#define XE2_NO_PROMOTE				REG_BIT(10)
#define XE2_COMP_EN				REG_BIT(9)
#define XE2_L3_CLOS				REG_GENMASK(7, 6)
#define XE2_L3_POLICY				REG_GENMASK(5, 4)
#define XE2_L4_POLICY				REG_GENMASK(3, 2)
#define XE2_COH_MODE				REG_GENMASK(1, 0)

#define XELPG_L4_POLICY_MASK			REG_GENMASK(3, 2)
#define XELPG_PAT_3_UC				REG_FIELD_PREP(XELPG_L4_POLICY_MASK, 3)
#define XELPG_PAT_1_WT				REG_FIELD_PREP(XELPG_L4_POLICY_MASK, 1)
#define XELPG_PAT_0_WB				REG_FIELD_PREP(XELPG_L4_POLICY_MASK, 0)
#define XELPG_INDEX_COH_MODE_MASK		REG_GENMASK(1, 0)
#define XELPG_3_COH_2W				REG_FIELD_PREP(XELPG_INDEX_COH_MODE_MASK, 3)
#define XELPG_2_COH_1W				REG_FIELD_PREP(XELPG_INDEX_COH_MODE_MASK, 2)
#define XELPG_0_COH_NON				REG_FIELD_PREP(XELPG_INDEX_COH_MODE_MASK, 0)

#define XEHPC_CLOS_LEVEL_MASK			REG_GENMASK(3, 2)
#define XEHPC_PAT_CLOS(x)			REG_FIELD_PREP(XEHPC_CLOS_LEVEL_MASK, x)

#define XELP_MEM_TYPE_MASK			REG_GENMASK(1, 0)
#define XELP_PAT_WB				REG_FIELD_PREP(XELP_MEM_TYPE_MASK, 3)
#define XELP_PAT_WT				REG_FIELD_PREP(XELP_MEM_TYPE_MASK, 2)
#define XELP_PAT_WC				REG_FIELD_PREP(XELP_MEM_TYPE_MASK, 1)
#define XELP_PAT_UC				REG_FIELD_PREP(XELP_MEM_TYPE_MASK, 0)

static const char *XELP_MEM_TYPE_STR_MAP[] = { "UC", "WC", "WT", "WB" };

struct xe_pat_ops {
	void (*program_graphics)(struct xe_gt *gt, const struct xe_pat_table_entry table[],
				 int n_entries);
	void (*program_media)(struct xe_gt *gt, const struct xe_pat_table_entry table[],
			      int n_entries);
	void (*dump)(struct xe_gt *gt, struct drm_printer *p);
};

static const struct xe_pat_table_entry xelp_pat_table[] = {
	[0] = { XELP_PAT_WB, XE_COH_AT_LEAST_1WAY },
	[1] = { XELP_PAT_WC, XE_COH_NONE },
	[2] = { XELP_PAT_WT, XE_COH_NONE },
	[3] = { XELP_PAT_UC, XE_COH_NONE },
};

static const struct xe_pat_table_entry xehpc_pat_table[] = {
	[0] = { XELP_PAT_UC, XE_COH_NONE },
	[1] = { XELP_PAT_WC, XE_COH_NONE },
	[2] = { XELP_PAT_WT, XE_COH_NONE },
	[3] = { XELP_PAT_WB, XE_COH_AT_LEAST_1WAY },
	[4] = { XEHPC_PAT_CLOS(1) | XELP_PAT_WT, XE_COH_NONE },
	[5] = { XEHPC_PAT_CLOS(1) | XELP_PAT_WB, XE_COH_AT_LEAST_1WAY },
	[6] = { XEHPC_PAT_CLOS(2) | XELP_PAT_WT, XE_COH_NONE },
	[7] = { XEHPC_PAT_CLOS(2) | XELP_PAT_WB, XE_COH_AT_LEAST_1WAY },
};

static const struct xe_pat_table_entry xelpg_pat_table[] = {
	[0] = { XELPG_PAT_0_WB, XE_COH_NONE },
	[1] = { XELPG_PAT_1_WT, XE_COH_NONE },
	[2] = { XELPG_PAT_3_UC, XE_COH_NONE },
	[3] = { XELPG_PAT_0_WB | XELPG_2_COH_1W, XE_COH_AT_LEAST_1WAY },
	[4] = { XELPG_PAT_0_WB | XELPG_3_COH_2W, XE_COH_AT_LEAST_1WAY },
};

/*
 * The Xe2 table is getting large/complicated so it's easier to review if
 * provided in a form that exactly matches the bspec's formatting.  The meaning
 * of the fields here are:
 *   - no_promote:  0=promotable, 1=no promote
 *   - comp_en:     0=disable, 1=enable
 *   - l3clos:      L3 class of service (0-3)
 *   - l3_policy:   0=WB, 1=XD ("WB - Transient Display"), 3=UC
 *   - l4_policy:   0=WB, 1=WT, 3=UC
 *   - coh_mode:    0=no snoop, 2=1-way coherent, 3=2-way coherent
 *
 * Reserved entries should be programmed with the maximum caching, minimum
 * coherency (which matches an all-0's encoding), so we can just omit them
 * in the table.
 *
 * Note: There is an implicit assumption in the driver that compression and
 * coh_1way+ are mutually exclusive. If this is ever not true then userptr
 * and imported dma-buf from external device will have uncleared ccs state.
 */
#define XE2_PAT(no_promote, comp_en, l3clos, l3_policy, l4_policy, __coh_mode) \
	{ \
		.value = (no_promote ? XE2_NO_PROMOTE : 0) | \
			(comp_en ? XE2_COMP_EN : 0) | \
			REG_FIELD_PREP(XE2_L3_CLOS, l3clos) | \
			REG_FIELD_PREP(XE2_L3_POLICY, l3_policy) | \
			REG_FIELD_PREP(XE2_L4_POLICY, l4_policy) | \
			REG_FIELD_PREP(XE2_COH_MODE, __coh_mode), \
		.coh_mode = (BUILD_BUG_ON_ZERO(__coh_mode && comp_en) || __coh_mode) ? \
			XE_COH_AT_LEAST_1WAY : XE_COH_NONE \
	}

static const struct xe_pat_table_entry xe2_pat_table[] = {
	[ 0] = XE2_PAT( 0, 0, 0, 0, 3, 0 ),
	[ 1] = XE2_PAT( 0, 0, 0, 0, 3, 2 ),
	[ 2] = XE2_PAT( 0, 0, 0, 0, 3, 3 ),
	[ 3] = XE2_PAT( 0, 0, 0, 3, 3, 0 ),
	[ 4] = XE2_PAT( 0, 0, 0, 3, 0, 2 ),
	[ 5] = XE2_PAT( 0, 0, 0, 3, 3, 2 ),
	[ 6] = XE2_PAT( 1, 0, 0, 1, 3, 0 ),
	[ 7] = XE2_PAT( 0, 0, 0, 3, 0, 3 ),
	[ 8] = XE2_PAT( 0, 0, 0, 3, 0, 0 ),
	[ 9] = XE2_PAT( 0, 1, 0, 0, 3, 0 ),
	[10] = XE2_PAT( 0, 1, 0, 3, 0, 0 ),
	[11] = XE2_PAT( 1, 1, 0, 1, 3, 0 ),
	[12] = XE2_PAT( 0, 1, 0, 3, 3, 0 ),
	[13] = XE2_PAT( 0, 0, 0, 0, 0, 0 ),
	[14] = XE2_PAT( 0, 1, 0, 0, 0, 0 ),
	[15] = XE2_PAT( 1, 1, 0, 1, 1, 0 ),
	/* 16..19 are reserved; leave set to all 0's */
	[20] = XE2_PAT( 0, 0, 1, 0, 3, 0 ),
	[21] = XE2_PAT( 0, 1, 1, 0, 3, 0 ),
	[22] = XE2_PAT( 0, 0, 1, 0, 3, 2 ),
	[23] = XE2_PAT( 0, 0, 1, 0, 3, 3 ),
	[24] = XE2_PAT( 0, 0, 2, 0, 3, 0 ),
	[25] = XE2_PAT( 0, 1, 2, 0, 3, 0 ),
	[26] = XE2_PAT( 0, 0, 2, 0, 3, 2 ),
	[27] = XE2_PAT( 0, 0, 2, 0, 3, 3 ),
	[28] = XE2_PAT( 0, 0, 3, 0, 3, 0 ),
	[29] = XE2_PAT( 0, 1, 3, 0, 3, 0 ),
	[30] = XE2_PAT( 0, 0, 3, 0, 3, 2 ),
	[31] = XE2_PAT( 0, 0, 3, 0, 3, 3 ),
};

/* Special PAT values programmed outside the main table */
static const struct xe_pat_table_entry xe2_pat_ats = XE2_PAT( 0, 0, 0, 0, 3, 3 );
static const struct xe_pat_table_entry xe2_pat_pta = XE2_PAT( 0, 0, 0, 0, 3, 0 );

u16 xe_pat_index_get_coh_mode(struct xe_device *xe, u16 pat_index)
{
	WARN_ON(pat_index >= xe->pat.n_entries);
	return xe->pat.table[pat_index].coh_mode;
}

static void program_pat(struct xe_gt *gt, const struct xe_pat_table_entry table[],
			int n_entries)
{
	for (int i = 0; i < n_entries; i++) {
		struct xe_reg reg = XE_REG(_PAT_INDEX(i));

		xe_mmio_write32(&gt->mmio, reg, table[i].value);
	}
}

static void program_pat_mcr(struct xe_gt *gt, const struct xe_pat_table_entry table[],
			    int n_entries)
{
	for (int i = 0; i < n_entries; i++) {
		struct xe_reg_mcr reg_mcr = XE_REG_MCR(_PAT_INDEX(i));

		xe_gt_mcr_multicast_write(gt, reg_mcr, table[i].value);
	}
}

static void xelp_dump(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int fw_ref;
	int i;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return;

	drm_printf(p, "PAT table:\n");

	for (i = 0; i < xe->pat.n_entries; i++) {
		u32 pat = xe_mmio_read32(&gt->mmio, XE_REG(_PAT_INDEX(i)));
		u8 mem_type = REG_FIELD_GET(XELP_MEM_TYPE_MASK, pat);

		drm_printf(p, "PAT[%2d] = %s (%#8x)\n", i,
			   XELP_MEM_TYPE_STR_MAP[mem_type], pat);
	}

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

static const struct xe_pat_ops xelp_pat_ops = {
	.program_graphics = program_pat,
	.dump = xelp_dump,
};

static void xehp_dump(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int fw_ref;
	int i;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return;

	drm_printf(p, "PAT table:\n");

	for (i = 0; i < xe->pat.n_entries; i++) {
		u32 pat = xe_gt_mcr_unicast_read_any(gt, XE_REG_MCR(_PAT_INDEX(i)));
		u8 mem_type;

		mem_type = REG_FIELD_GET(XELP_MEM_TYPE_MASK, pat);

		drm_printf(p, "PAT[%2d] = %s (%#8x)\n", i,
			   XELP_MEM_TYPE_STR_MAP[mem_type], pat);
	}

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

static const struct xe_pat_ops xehp_pat_ops = {
	.program_graphics = program_pat_mcr,
	.dump = xehp_dump,
};

static void xehpc_dump(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int fw_ref;
	int i;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return;

	drm_printf(p, "PAT table:\n");

	for (i = 0; i < xe->pat.n_entries; i++) {
		u32 pat = xe_gt_mcr_unicast_read_any(gt, XE_REG_MCR(_PAT_INDEX(i)));

		drm_printf(p, "PAT[%2d] = [ %u, %u ] (%#8x)\n", i,
			   REG_FIELD_GET(XELP_MEM_TYPE_MASK, pat),
			   REG_FIELD_GET(XEHPC_CLOS_LEVEL_MASK, pat), pat);
	}

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

static const struct xe_pat_ops xehpc_pat_ops = {
	.program_graphics = program_pat_mcr,
	.dump = xehpc_dump,
};

static void xelpg_dump(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int fw_ref;
	int i;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return;

	drm_printf(p, "PAT table:\n");

	for (i = 0; i < xe->pat.n_entries; i++) {
		u32 pat;

		if (xe_gt_is_media_type(gt))
			pat = xe_mmio_read32(&gt->mmio, XE_REG(_PAT_INDEX(i)));
		else
			pat = xe_gt_mcr_unicast_read_any(gt, XE_REG_MCR(_PAT_INDEX(i)));

		drm_printf(p, "PAT[%2d] = [ %u, %u ] (%#8x)\n", i,
			   REG_FIELD_GET(XELPG_L4_POLICY_MASK, pat),
			   REG_FIELD_GET(XELPG_INDEX_COH_MODE_MASK, pat), pat);
	}

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

/*
 * SAMedia register offsets are adjusted by the write methods and they target
 * registers that are not MCR, while for normal GT they are MCR
 */
static const struct xe_pat_ops xelpg_pat_ops = {
	.program_graphics = program_pat,
	.program_media = program_pat_mcr,
	.dump = xelpg_dump,
};

static void xe2lpg_program_pat(struct xe_gt *gt, const struct xe_pat_table_entry table[],
			       int n_entries)
{
	program_pat_mcr(gt, table, n_entries);
	xe_gt_mcr_multicast_write(gt, XE_REG_MCR(_PAT_ATS), xe2_pat_ats.value);

	if (IS_DGFX(gt_to_xe(gt)))
		xe_gt_mcr_multicast_write(gt, XE_REG_MCR(_PAT_PTA), xe2_pat_pta.value);
}

static void xe2lpm_program_pat(struct xe_gt *gt, const struct xe_pat_table_entry table[],
			       int n_entries)
{
	program_pat(gt, table, n_entries);
	xe_mmio_write32(&gt->mmio, XE_REG(_PAT_ATS), xe2_pat_ats.value);

	if (IS_DGFX(gt_to_xe(gt)))
		xe_mmio_write32(&gt->mmio, XE_REG(_PAT_PTA), xe2_pat_pta.value);
}

static void xe2_dump(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int fw_ref;
	u32 pat;
	int i;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (!fw_ref)
		return;

	drm_printf(p, "PAT table:\n");

	for (i = 0; i < xe->pat.n_entries; i++) {
		if (xe_gt_is_media_type(gt))
			pat = xe_mmio_read32(&gt->mmio, XE_REG(_PAT_INDEX(i)));
		else
			pat = xe_gt_mcr_unicast_read_any(gt, XE_REG_MCR(_PAT_INDEX(i)));

		drm_printf(p, "PAT[%2d] = [ %u, %u, %u, %u, %u, %u ]  (%#8x)\n", i,
			   !!(pat & XE2_NO_PROMOTE),
			   !!(pat & XE2_COMP_EN),
			   REG_FIELD_GET(XE2_L3_CLOS, pat),
			   REG_FIELD_GET(XE2_L3_POLICY, pat),
			   REG_FIELD_GET(XE2_L4_POLICY, pat),
			   REG_FIELD_GET(XE2_COH_MODE, pat),
			   pat);
	}

	/*
	 * Also print PTA_MODE, which describes how the hardware accesses
	 * PPGTT entries.
	 */
	if (xe_gt_is_media_type(gt))
		pat = xe_mmio_read32(&gt->mmio, XE_REG(_PAT_PTA));
	else
		pat = xe_gt_mcr_unicast_read_any(gt, XE_REG_MCR(_PAT_PTA));

	drm_printf(p, "Page Table Access:\n");
	drm_printf(p, "PTA_MODE= [ %u, %u, %u, %u, %u, %u ]  (%#8x)\n",
		   !!(pat & XE2_NO_PROMOTE),
		   !!(pat & XE2_COMP_EN),
		   REG_FIELD_GET(XE2_L3_CLOS, pat),
		   REG_FIELD_GET(XE2_L3_POLICY, pat),
		   REG_FIELD_GET(XE2_L4_POLICY, pat),
		   REG_FIELD_GET(XE2_COH_MODE, pat),
		   pat);

	xe_force_wake_put(gt_to_fw(gt), fw_ref);
}

static const struct xe_pat_ops xe2_pat_ops = {
	.program_graphics = xe2lpg_program_pat,
	.program_media = xe2lpm_program_pat,
	.dump = xe2_dump,
};

void xe_pat_init_early(struct xe_device *xe)
{
	if (GRAPHICS_VER(xe) == 30 || GRAPHICS_VER(xe) == 20) {
		xe->pat.ops = &xe2_pat_ops;
		xe->pat.table = xe2_pat_table;

		/* Wa_16023588340. XXX: Should use XE_WA */
		if (GRAPHICS_VERx100(xe) == 2001)
			xe->pat.n_entries = 28; /* Disable CLOS3 */
		else
			xe->pat.n_entries = ARRAY_SIZE(xe2_pat_table);

		xe->pat.idx[XE_CACHE_NONE] = 3;
		xe->pat.idx[XE_CACHE_WT] = 15;
		xe->pat.idx[XE_CACHE_WB] = 2;
		xe->pat.idx[XE_CACHE_NONE_COMPRESSION] = 12; /*Applicable on xe2 and beyond */
	} else if (xe->info.platform == XE_METEORLAKE) {
		xe->pat.ops = &xelpg_pat_ops;
		xe->pat.table = xelpg_pat_table;
		xe->pat.n_entries = ARRAY_SIZE(xelpg_pat_table);
		xe->pat.idx[XE_CACHE_NONE] = 2;
		xe->pat.idx[XE_CACHE_WT] = 1;
		xe->pat.idx[XE_CACHE_WB] = 3;
	} else if (xe->info.platform == XE_PVC) {
		xe->pat.ops = &xehpc_pat_ops;
		xe->pat.table = xehpc_pat_table;
		xe->pat.n_entries = ARRAY_SIZE(xehpc_pat_table);
		xe->pat.idx[XE_CACHE_NONE] = 0;
		xe->pat.idx[XE_CACHE_WT] = 2;
		xe->pat.idx[XE_CACHE_WB] = 3;
	} else if (xe->info.platform == XE_DG2) {
		/*
		 * Table is the same as previous platforms, but programming
		 * method has changed.
		 */
		xe->pat.ops = &xehp_pat_ops;
		xe->pat.table = xelp_pat_table;
		xe->pat.n_entries = ARRAY_SIZE(xelp_pat_table);
		xe->pat.idx[XE_CACHE_NONE] = 3;
		xe->pat.idx[XE_CACHE_WT] = 2;
		xe->pat.idx[XE_CACHE_WB] = 0;
	} else if (GRAPHICS_VERx100(xe) <= 1210) {
		WARN_ON_ONCE(!IS_DGFX(xe) && !xe->info.has_llc);
		xe->pat.ops = &xelp_pat_ops;
		xe->pat.table = xelp_pat_table;
		xe->pat.n_entries = ARRAY_SIZE(xelp_pat_table);
		xe->pat.idx[XE_CACHE_NONE] = 3;
		xe->pat.idx[XE_CACHE_WT] = 2;
		xe->pat.idx[XE_CACHE_WB] = 0;
	} else {
		/*
		 * Going forward we expect to need new PAT settings for most
		 * new platforms; failure to provide a new table can easily
		 * lead to subtle, hard-to-debug problems.  If none of the
		 * conditions above match the platform we're running on we'll
		 * raise an error rather than trying to silently inherit the
		 * most recent platform's behavior.
		 */
		drm_err(&xe->drm, "Missing PAT table for platform with graphics version %d.%02d!\n",
			GRAPHICS_VER(xe), GRAPHICS_VERx100(xe) % 100);
	}

	/* VFs can't program nor dump PAT settings */
	if (IS_SRIOV_VF(xe))
		xe->pat.ops = NULL;

	xe_assert(xe, !xe->pat.ops || xe->pat.ops->dump);
	xe_assert(xe, !xe->pat.ops || xe->pat.ops->program_graphics);
	xe_assert(xe, !xe->pat.ops || MEDIA_VER(xe) < 13 || xe->pat.ops->program_media);
}

void xe_pat_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (!xe->pat.ops)
		return;

	if (xe_gt_is_media_type(gt))
		xe->pat.ops->program_media(gt, xe->pat.table, xe->pat.n_entries);
	else
		xe->pat.ops->program_graphics(gt, xe->pat.table, xe->pat.n_entries);
}

void xe_pat_dump(struct xe_gt *gt, struct drm_printer *p)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (!xe->pat.ops)
		return;

	xe->pat.ops->dump(gt, p);
}

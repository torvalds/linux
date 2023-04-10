// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2022 Intel Corporation
 */

#include <linux/types.h>

#include <drm/drm_print.h>

#include "gt/intel_engine_regs.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_mcr.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_lrc.h"
#include "guc_capture_fwif.h"
#include "intel_guc_capture.h"
#include "intel_guc_fwif.h"
#include "i915_drv.h"
#include "i915_gpu_error.h"
#include "i915_irq.h"
#include "i915_memcpy.h"
#include "i915_reg.h"

/*
 * Define all device tables of GuC error capture register lists
 * NOTE: For engine-registers, GuC only needs the register offsets
 *       from the engine-mmio-base
 */
#define COMMON_BASE_GLOBAL \
	{ FORCEWAKE_MT,             0,      0, "FORCEWAKE" }

#define COMMON_GEN9BASE_GLOBAL \
	{ GEN8_FAULT_TLB_DATA0,     0,      0, "GEN8_FAULT_TLB_DATA0" }, \
	{ GEN8_FAULT_TLB_DATA1,     0,      0, "GEN8_FAULT_TLB_DATA1" }, \
	{ ERROR_GEN6,               0,      0, "ERROR_GEN6" }, \
	{ DONE_REG,                 0,      0, "DONE_REG" }, \
	{ HSW_GTT_CACHE_EN,         0,      0, "HSW_GTT_CACHE_EN" }

#define COMMON_GEN12BASE_GLOBAL \
	{ GEN12_FAULT_TLB_DATA0,    0,      0, "GEN12_FAULT_TLB_DATA0" }, \
	{ GEN12_FAULT_TLB_DATA1,    0,      0, "GEN12_FAULT_TLB_DATA1" }, \
	{ GEN12_AUX_ERR_DBG,        0,      0, "AUX_ERR_DBG" }, \
	{ GEN12_GAM_DONE,           0,      0, "GAM_DONE" }, \
	{ GEN12_RING_FAULT_REG,     0,      0, "FAULT_REG" }

#define COMMON_BASE_ENGINE_INSTANCE \
	{ RING_PSMI_CTL(0),         0,      0, "RC PSMI" }, \
	{ RING_ESR(0),              0,      0, "ESR" }, \
	{ RING_DMA_FADD(0),         0,      0, "RING_DMA_FADD_LDW" }, \
	{ RING_DMA_FADD_UDW(0),     0,      0, "RING_DMA_FADD_UDW" }, \
	{ RING_IPEIR(0),            0,      0, "IPEIR" }, \
	{ RING_IPEHR(0),            0,      0, "IPEHR" }, \
	{ RING_INSTPS(0),           0,      0, "INSTPS" }, \
	{ RING_BBADDR(0),           0,      0, "RING_BBADDR_LOW32" }, \
	{ RING_BBADDR_UDW(0),       0,      0, "RING_BBADDR_UP32" }, \
	{ RING_BBSTATE(0),          0,      0, "BB_STATE" }, \
	{ CCID(0),                  0,      0, "CCID" }, \
	{ RING_ACTHD(0),            0,      0, "ACTHD_LDW" }, \
	{ RING_ACTHD_UDW(0),        0,      0, "ACTHD_UDW" }, \
	{ RING_INSTPM(0),           0,      0, "INSTPM" }, \
	{ RING_INSTDONE(0),         0,      0, "INSTDONE" }, \
	{ RING_NOPID(0),            0,      0, "RING_NOPID" }, \
	{ RING_START(0),            0,      0, "START" }, \
	{ RING_HEAD(0),             0,      0, "HEAD" }, \
	{ RING_TAIL(0),             0,      0, "TAIL" }, \
	{ RING_CTL(0),              0,      0, "CTL" }, \
	{ RING_MI_MODE(0),          0,      0, "MODE" }, \
	{ RING_CONTEXT_CONTROL(0),  0,      0, "RING_CONTEXT_CONTROL" }, \
	{ RING_HWS_PGA(0),          0,      0, "HWS" }, \
	{ RING_MODE_GEN7(0),        0,      0, "GFX_MODE" }, \
	{ GEN8_RING_PDP_LDW(0, 0),  0,      0, "PDP0_LDW" }, \
	{ GEN8_RING_PDP_UDW(0, 0),  0,      0, "PDP0_UDW" }, \
	{ GEN8_RING_PDP_LDW(0, 1),  0,      0, "PDP1_LDW" }, \
	{ GEN8_RING_PDP_UDW(0, 1),  0,      0, "PDP1_UDW" }, \
	{ GEN8_RING_PDP_LDW(0, 2),  0,      0, "PDP2_LDW" }, \
	{ GEN8_RING_PDP_UDW(0, 2),  0,      0, "PDP2_UDW" }, \
	{ GEN8_RING_PDP_LDW(0, 3),  0,      0, "PDP3_LDW" }, \
	{ GEN8_RING_PDP_UDW(0, 3),  0,      0, "PDP3_UDW" }

#define COMMON_BASE_HAS_EU \
	{ EIR,                      0,      0, "EIR" }

#define COMMON_BASE_RENDER \
	{ GEN7_SC_INSTDONE,         0,      0, "GEN7_SC_INSTDONE" }

#define COMMON_GEN12BASE_RENDER \
	{ GEN12_SC_INSTDONE_EXTRA,  0,      0, "GEN12_SC_INSTDONE_EXTRA" }, \
	{ GEN12_SC_INSTDONE_EXTRA2, 0,      0, "GEN12_SC_INSTDONE_EXTRA2" }

#define COMMON_GEN12BASE_VEC \
	{ GEN12_SFC_DONE(0),        0,      0, "SFC_DONE[0]" }, \
	{ GEN12_SFC_DONE(1),        0,      0, "SFC_DONE[1]" }, \
	{ GEN12_SFC_DONE(2),        0,      0, "SFC_DONE[2]" }, \
	{ GEN12_SFC_DONE(3),        0,      0, "SFC_DONE[3]" }

/* XE_LPD - Global */
static const struct __guc_mmio_reg_descr xe_lpd_global_regs[] = {
	COMMON_BASE_GLOBAL,
	COMMON_GEN9BASE_GLOBAL,
	COMMON_GEN12BASE_GLOBAL,
};

/* XE_LPD - Render / Compute Per-Class */
static const struct __guc_mmio_reg_descr xe_lpd_rc_class_regs[] = {
	COMMON_BASE_HAS_EU,
	COMMON_BASE_RENDER,
	COMMON_GEN12BASE_RENDER,
};

/* GEN9/XE_LPD - Render / Compute Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_rc_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE,
};

/* GEN9/XE_LPD - Media Decode/Encode Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_vd_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE,
};

/* XE_LPD - Video Enhancement Per-Class */
static const struct __guc_mmio_reg_descr xe_lpd_vec_class_regs[] = {
	COMMON_GEN12BASE_VEC,
};

/* GEN9/XE_LPD - Video Enhancement Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_vec_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE,
};

/* GEN9/XE_LPD - Blitter Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_blt_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE,
};

/* XE_LPD - GSC Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_gsc_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE,
};

/* GEN9 - Global */
static const struct __guc_mmio_reg_descr default_global_regs[] = {
	COMMON_BASE_GLOBAL,
	COMMON_GEN9BASE_GLOBAL,
};

static const struct __guc_mmio_reg_descr default_rc_class_regs[] = {
	COMMON_BASE_HAS_EU,
	COMMON_BASE_RENDER,
};

/*
 * Empty lists:
 * GEN9/XE_LPD - Blitter Per-Class
 * GEN9/XE_LPD - Media Decode/Encode Per-Class
 * GEN9 - VEC Class
 */
static const struct __guc_mmio_reg_descr empty_regs_list[] = {
};

#define TO_GCAP_DEF_OWNER(x) (GUC_CAPTURE_LIST_INDEX_##x)
#define TO_GCAP_DEF_TYPE(x) (GUC_CAPTURE_LIST_TYPE_##x)
#define MAKE_REGLIST(regslist, regsowner, regstype, class) \
	{ \
		regslist, \
		ARRAY_SIZE(regslist), \
		TO_GCAP_DEF_OWNER(regsowner), \
		TO_GCAP_DEF_TYPE(regstype), \
		class, \
		NULL, \
	}

/* List of lists */
static const struct __guc_mmio_reg_descr_group default_lists[] = {
	MAKE_REGLIST(default_global_regs, PF, GLOBAL, 0),
	MAKE_REGLIST(default_rc_class_regs, PF, ENGINE_CLASS, GUC_RENDER_CLASS),
	MAKE_REGLIST(xe_lpd_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_RENDER_CLASS),
	MAKE_REGLIST(default_rc_class_regs, PF, ENGINE_CLASS, GUC_COMPUTE_CLASS),
	MAKE_REGLIST(xe_lpd_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_COMPUTE_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vd_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEO_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(xe_lpd_vec_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_BLITTER_CLASS),
	MAKE_REGLIST(xe_lpd_blt_inst_regs, PF, ENGINE_INSTANCE, GUC_BLITTER_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_GSC_OTHER_CLASS),
	MAKE_REGLIST(xe_lpd_gsc_inst_regs, PF, ENGINE_INSTANCE, GUC_GSC_OTHER_CLASS),
	{}
};

static const struct __guc_mmio_reg_descr_group xe_lpd_lists[] = {
	MAKE_REGLIST(xe_lpd_global_regs, PF, GLOBAL, 0),
	MAKE_REGLIST(xe_lpd_rc_class_regs, PF, ENGINE_CLASS, GUC_RENDER_CLASS),
	MAKE_REGLIST(xe_lpd_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_RENDER_CLASS),
	MAKE_REGLIST(xe_lpd_rc_class_regs, PF, ENGINE_CLASS, GUC_COMPUTE_CLASS),
	MAKE_REGLIST(xe_lpd_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_COMPUTE_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vd_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vec_class_regs, PF, ENGINE_CLASS, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(xe_lpd_vec_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_BLITTER_CLASS),
	MAKE_REGLIST(xe_lpd_blt_inst_regs, PF, ENGINE_INSTANCE, GUC_BLITTER_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_GSC_OTHER_CLASS),
	MAKE_REGLIST(xe_lpd_gsc_inst_regs, PF, ENGINE_INSTANCE, GUC_GSC_OTHER_CLASS),
	{}
};

static const struct __guc_mmio_reg_descr_group *
guc_capture_get_one_list(const struct __guc_mmio_reg_descr_group *reglists,
			 u32 owner, u32 type, u32 id)
{
	int i;

	if (!reglists)
		return NULL;

	for (i = 0; reglists[i].list; ++i) {
		if (reglists[i].owner == owner && reglists[i].type == type &&
		    (reglists[i].engine == id || reglists[i].type == GUC_CAPTURE_LIST_TYPE_GLOBAL))
			return &reglists[i];
	}

	return NULL;
}

static struct __guc_mmio_reg_descr_group *
guc_capture_get_one_ext_list(struct __guc_mmio_reg_descr_group *reglists,
			     u32 owner, u32 type, u32 id)
{
	int i;

	if (!reglists)
		return NULL;

	for (i = 0; reglists[i].extlist; ++i) {
		if (reglists[i].owner == owner && reglists[i].type == type &&
		    (reglists[i].engine == id || reglists[i].type == GUC_CAPTURE_LIST_TYPE_GLOBAL))
			return &reglists[i];
	}

	return NULL;
}

static void guc_capture_free_extlists(struct __guc_mmio_reg_descr_group *reglists)
{
	int i = 0;

	if (!reglists)
		return;

	while (reglists[i].extlist)
		kfree(reglists[i++].extlist);
}

struct __ext_steer_reg {
	const char *name;
	i915_mcr_reg_t reg;
};

static const struct __ext_steer_reg xe_extregs[] = {
	{"GEN8_SAMPLER_INSTDONE", GEN8_SAMPLER_INSTDONE},
	{"GEN8_ROW_INSTDONE", GEN8_ROW_INSTDONE}
};

static void __fill_ext_reg(struct __guc_mmio_reg_descr *ext,
			   const struct __ext_steer_reg *extlist,
			   int slice_id, int subslice_id)
{
	ext->reg = _MMIO(i915_mmio_reg_offset(extlist->reg));
	ext->flags = FIELD_PREP(GUC_REGSET_STEERING_GROUP, slice_id);
	ext->flags |= FIELD_PREP(GUC_REGSET_STEERING_INSTANCE, subslice_id);
	ext->regname = extlist->name;
}

static int
__alloc_ext_regs(struct __guc_mmio_reg_descr_group *newlist,
		 const struct __guc_mmio_reg_descr_group *rootlist, int num_regs)
{
	struct __guc_mmio_reg_descr *list;

	list = kcalloc(num_regs, sizeof(struct __guc_mmio_reg_descr), GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	newlist->extlist = list;
	newlist->num_regs = num_regs;
	newlist->owner = rootlist->owner;
	newlist->engine = rootlist->engine;
	newlist->type = rootlist->type;

	return 0;
}

static void
guc_capture_alloc_steered_lists_xe_lpd(struct intel_guc *guc,
				       const struct __guc_mmio_reg_descr_group *lists)
{
	struct intel_gt *gt = guc_to_gt(guc);
	int slice, subslice, iter, i, num_steer_regs, num_tot_regs = 0;
	const struct __guc_mmio_reg_descr_group *list;
	struct __guc_mmio_reg_descr_group *extlists;
	struct __guc_mmio_reg_descr *extarray;
	struct sseu_dev_info *sseu;

	/* In XE_LPD we only have steered registers for the render-class */
	list = guc_capture_get_one_list(lists, GUC_CAPTURE_LIST_INDEX_PF,
					GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS, GUC_RENDER_CLASS);
	/* skip if extlists was previously allocated */
	if (!list || guc->capture->extlists)
		return;

	num_steer_regs = ARRAY_SIZE(xe_extregs);

	sseu = &gt->info.sseu;
	for_each_ss_steering(iter, gt, slice, subslice)
		num_tot_regs += num_steer_regs;

	if (!num_tot_regs)
		return;

	/* allocate an extra for an end marker */
	extlists = kcalloc(2, sizeof(struct __guc_mmio_reg_descr_group), GFP_KERNEL);
	if (!extlists)
		return;

	if (__alloc_ext_regs(&extlists[0], list, num_tot_regs)) {
		kfree(extlists);
		return;
	}

	extarray = extlists[0].extlist;
	for_each_ss_steering(iter, gt, slice, subslice) {
		for (i = 0; i < num_steer_regs; ++i) {
			__fill_ext_reg(extarray, &xe_extregs[i], slice, subslice);
			++extarray;
		}
	}

	guc->capture->extlists = extlists;
}

static const struct __ext_steer_reg xehpg_extregs[] = {
	{"XEHPG_INSTDONE_GEOM_SVG", XEHPG_INSTDONE_GEOM_SVG}
};

static bool __has_xehpg_extregs(u32 ipver)
{
	return (ipver >= IP_VER(12, 55));
}

static void
guc_capture_alloc_steered_lists_xe_hpg(struct intel_guc *guc,
				       const struct __guc_mmio_reg_descr_group *lists,
				       u32 ipver)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct sseu_dev_info *sseu;
	int slice, subslice, i, iter, num_steer_regs, num_tot_regs = 0;
	const struct __guc_mmio_reg_descr_group *list;
	struct __guc_mmio_reg_descr_group *extlists;
	struct __guc_mmio_reg_descr *extarray;

	/* In XE_LP / HPG we only have render-class steering registers during error-capture */
	list = guc_capture_get_one_list(lists, GUC_CAPTURE_LIST_INDEX_PF,
					GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS, GUC_RENDER_CLASS);
	/* skip if extlists was previously allocated */
	if (!list || guc->capture->extlists)
		return;

	num_steer_regs = ARRAY_SIZE(xe_extregs);
	if (__has_xehpg_extregs(ipver))
		num_steer_regs += ARRAY_SIZE(xehpg_extregs);

	sseu = &gt->info.sseu;
	for_each_ss_steering(iter, gt, slice, subslice)
		num_tot_regs += num_steer_regs;

	if (!num_tot_regs)
		return;

	/* allocate an extra for an end marker */
	extlists = kcalloc(2, sizeof(struct __guc_mmio_reg_descr_group), GFP_KERNEL);
	if (!extlists)
		return;

	if (__alloc_ext_regs(&extlists[0], list, num_tot_regs)) {
		kfree(extlists);
		return;
	}

	extarray = extlists[0].extlist;
	for_each_ss_steering(iter, gt, slice, subslice) {
		for (i = 0; i < ARRAY_SIZE(xe_extregs); ++i) {
			__fill_ext_reg(extarray, &xe_extregs[i], slice, subslice);
			++extarray;
		}
		if (__has_xehpg_extregs(ipver)) {
			for (i = 0; i < ARRAY_SIZE(xehpg_extregs); ++i) {
				__fill_ext_reg(extarray, &xehpg_extregs[i], slice, subslice);
				++extarray;
			}
		}
	}

	drm_dbg(&i915->drm, "GuC-capture found %d-ext-regs.\n", num_tot_regs);
	guc->capture->extlists = extlists;
}

static const struct __guc_mmio_reg_descr_group *
guc_capture_get_device_reglist(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	if (GRAPHICS_VER(i915) > 11) {
		/*
		 * For certain engine classes, there are slice and subslice
		 * level registers requiring steering. We allocate and populate
		 * these at init time based on hw config add it as an extension
		 * list at the end of the pre-populated render list.
		 */
		if (IS_DG2(i915))
			guc_capture_alloc_steered_lists_xe_hpg(guc, xe_lpd_lists, IP_VER(12, 55));
		else if (IS_XEHPSDV(i915))
			guc_capture_alloc_steered_lists_xe_hpg(guc, xe_lpd_lists, IP_VER(12, 50));
		else
			guc_capture_alloc_steered_lists_xe_lpd(guc, xe_lpd_lists);

		return xe_lpd_lists;
	}

	/* if GuC submission is enabled on a non-POR platform, just use a common baseline */
	return default_lists;
}

static const char *
__stringify_type(u32 type)
{
	switch (type) {
	case GUC_CAPTURE_LIST_TYPE_GLOBAL:
		return "Global";
	case GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS:
		return "Class";
	case GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE:
		return "Instance";
	default:
		break;
	}

	return "unknown";
}

static const char *
__stringify_engclass(u32 class)
{
	switch (class) {
	case GUC_RENDER_CLASS:
		return "Render";
	case GUC_VIDEO_CLASS:
		return "Video";
	case GUC_VIDEOENHANCE_CLASS:
		return "VideoEnhance";
	case GUC_BLITTER_CLASS:
		return "Blitter";
	case GUC_COMPUTE_CLASS:
		return "Compute";
	case GUC_GSC_OTHER_CLASS:
		return "GSC-Other";
	default:
		break;
	}

	return "unknown";
}

static int
guc_capture_list_init(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
		      struct guc_mmio_reg *ptr, u16 num_entries)
{
	u32 i = 0, j = 0;
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	const struct __guc_mmio_reg_descr_group *reglists = guc->capture->reglists;
	struct __guc_mmio_reg_descr_group *extlists = guc->capture->extlists;
	const struct __guc_mmio_reg_descr_group *match;
	struct __guc_mmio_reg_descr_group *matchext;

	if (!reglists)
		return -ENODEV;

	match = guc_capture_get_one_list(reglists, owner, type, classid);
	if (!match)
		return -ENODATA;

	for (i = 0; i < num_entries && i < match->num_regs; ++i) {
		ptr[i].offset = match->list[i].reg.reg;
		ptr[i].value = 0xDEADF00D;
		ptr[i].flags = match->list[i].flags;
		ptr[i].mask = match->list[i].mask;
	}

	matchext = guc_capture_get_one_ext_list(extlists, owner, type, classid);
	if (matchext) {
		for (i = match->num_regs, j = 0; i < num_entries &&
		     i < (match->num_regs + matchext->num_regs) &&
			j < matchext->num_regs; ++i, ++j) {
			ptr[i].offset = matchext->extlist[j].reg.reg;
			ptr[i].value = 0xDEADF00D;
			ptr[i].flags = matchext->extlist[j].flags;
			ptr[i].mask = matchext->extlist[j].mask;
		}
	}
	if (i < num_entries)
		drm_dbg(&i915->drm, "GuC-capture: Init reglist short %d out %d.\n",
			(int)i, (int)num_entries);

	return 0;
}

static int
guc_cap_list_num_regs(struct intel_guc_state_capture *gc, u32 owner, u32 type, u32 classid)
{
	const struct __guc_mmio_reg_descr_group *match;
	struct __guc_mmio_reg_descr_group *matchext;
	int num_regs;

	match = guc_capture_get_one_list(gc->reglists, owner, type, classid);
	if (!match)
		return 0;

	num_regs = match->num_regs;

	matchext = guc_capture_get_one_ext_list(gc->extlists, owner, type, classid);
	if (matchext)
		num_regs += matchext->num_regs;

	return num_regs;
}

static int
guc_capture_getlistsize(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
			size_t *size, bool is_purpose_est)
{
	struct intel_guc_state_capture *gc = guc->capture;
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct __guc_capture_ads_cache *cache = &gc->ads_cache[owner][type][classid];
	int num_regs;

	if (!gc->reglists) {
		drm_warn(&i915->drm, "GuC-capture: No reglist on this device\n");
		return -ENODEV;
	}

	if (cache->is_valid) {
		*size = cache->size;
		return cache->status;
	}

	if (!is_purpose_est && owner == GUC_CAPTURE_LIST_INDEX_PF &&
	    !guc_capture_get_one_list(gc->reglists, owner, type, classid)) {
		if (type == GUC_CAPTURE_LIST_TYPE_GLOBAL)
			drm_warn(&i915->drm, "Missing GuC-Err-Cap reglist Global!\n");
		else
			drm_warn(&i915->drm, "Missing GuC-Err-Cap reglist %s(%u):%s(%u)!\n",
				 __stringify_type(type), type,
				 __stringify_engclass(classid), classid);
		return -ENODATA;
	}

	num_regs = guc_cap_list_num_regs(gc, owner, type, classid);
	/* intentional empty lists can exist depending on hw config */
	if (!num_regs)
		return -ENODATA;

	if (size)
		*size = PAGE_ALIGN((sizeof(struct guc_debug_capture_list)) +
				   (num_regs * sizeof(struct guc_mmio_reg)));

	return 0;
}

int
intel_guc_capture_getlistsize(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
			      size_t *size)
{
	return guc_capture_getlistsize(guc, owner, type, classid, size, false);
}

static void guc_capture_create_prealloc_nodes(struct intel_guc *guc);

int
intel_guc_capture_getlist(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
			  void **outptr)
{
	struct intel_guc_state_capture *gc = guc->capture;
	struct __guc_capture_ads_cache *cache = &gc->ads_cache[owner][type][classid];
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct guc_debug_capture_list *listnode;
	int ret, num_regs;
	u8 *caplist, *tmp;
	size_t size = 0;

	if (!gc->reglists)
		return -ENODEV;

	if (cache->is_valid) {
		*outptr = cache->ptr;
		return cache->status;
	}

	/*
	 * ADS population of input registers is a good
	 * time to pre-allocate cachelist output nodes
	 */
	guc_capture_create_prealloc_nodes(guc);

	ret = intel_guc_capture_getlistsize(guc, owner, type, classid, &size);
	if (ret) {
		cache->is_valid = true;
		cache->ptr = NULL;
		cache->size = 0;
		cache->status = ret;
		return ret;
	}

	caplist = kzalloc(size, GFP_KERNEL);
	if (!caplist) {
		drm_dbg(&i915->drm, "GuC-capture: failed to alloc cached caplist");
		return -ENOMEM;
	}

	/* populate capture list header */
	tmp = caplist;
	num_regs = guc_cap_list_num_regs(guc->capture, owner, type, classid);
	listnode = (struct guc_debug_capture_list *)tmp;
	listnode->header.info = FIELD_PREP(GUC_CAPTURELISTHDR_NUMDESCR, (u32)num_regs);

	/* populate list of register descriptor */
	tmp += sizeof(struct guc_debug_capture_list);
	guc_capture_list_init(guc, owner, type, classid, (struct guc_mmio_reg *)tmp, num_regs);

	/* cache this list */
	cache->is_valid = true;
	cache->ptr = caplist;
	cache->size = size;
	cache->status = 0;

	*outptr = caplist;

	return 0;
}

int
intel_guc_capture_getnullheader(struct intel_guc *guc,
				void **outptr, size_t *size)
{
	struct intel_guc_state_capture *gc = guc->capture;
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	int tmp = sizeof(u32) * 4;
	void *null_header;

	if (gc->ads_null_cache) {
		*outptr = gc->ads_null_cache;
		*size = tmp;
		return 0;
	}

	null_header = kzalloc(tmp, GFP_KERNEL);
	if (!null_header) {
		drm_dbg(&i915->drm, "GuC-capture: failed to alloc cached nulllist");
		return -ENOMEM;
	}

	gc->ads_null_cache = null_header;
	*outptr = null_header;
	*size = tmp;

	return 0;
}

static int
guc_capture_output_min_size_est(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int worst_min_size = 0;
	size_t tmp = 0;

	if (!guc->capture)
		return -ENODEV;

	/*
	 * If every single engine-instance suffered a failure in quick succession but
	 * were all unrelated, then a burst of multiple error-capture events would dump
	 * registers for every one engine instance, one at a time. In this case, GuC
	 * would even dump the global-registers repeatedly.
	 *
	 * For each engine instance, there would be 1 x guc_state_capture_group_t output
	 * followed by 3 x guc_state_capture_t lists. The latter is how the register
	 * dumps are split across different register types (where the '3' are global vs class
	 * vs instance).
	 */
	for_each_engine(engine, gt, id) {
		worst_min_size += sizeof(struct guc_state_capture_group_header_t) +
					 (3 * sizeof(struct guc_state_capture_header_t));

		if (!guc_capture_getlistsize(guc, 0, GUC_CAPTURE_LIST_TYPE_GLOBAL, 0, &tmp, true))
			worst_min_size += tmp;

		if (!guc_capture_getlistsize(guc, 0, GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS,
					     engine->class, &tmp, true)) {
			worst_min_size += tmp;
		}
		if (!guc_capture_getlistsize(guc, 0, GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE,
					     engine->class, &tmp, true)) {
			worst_min_size += tmp;
		}
	}

	return worst_min_size;
}

/*
 * Add on a 3x multiplier to allow for multiple back-to-back captures occurring
 * before the i915 can read the data out and process it
 */
#define GUC_CAPTURE_OVERBUFFER_MULTIPLIER 3

static void check_guc_capture_size(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	int min_size = guc_capture_output_min_size_est(guc);
	int spare_size = min_size * GUC_CAPTURE_OVERBUFFER_MULTIPLIER;
	u32 buffer_size = intel_guc_log_section_size_capture(&guc->log);

	/*
	 * NOTE: min_size is much smaller than the capture region allocation (DG2: <80K vs 1MB)
	 * Additionally, its based on space needed to fit all engines getting reset at once
	 * within the same G2H handler task slot. This is very unlikely. However, if GuC really
	 * does run out of space for whatever reason, we will see an separate warning message
	 * when processing the G2H event capture-notification, search for:
	 * INTEL_GUC_STATE_CAPTURE_EVENT_STATUS_NOSPACE.
	 */
	if (min_size < 0)
		drm_warn(&i915->drm, "Failed to calculate GuC error state capture buffer minimum size: %d!\n",
			 min_size);
	else if (min_size > buffer_size)
		drm_warn(&i915->drm, "GuC error state capture buffer maybe small: %d < %d\n",
			 buffer_size, min_size);
	else if (spare_size > buffer_size)
		drm_dbg(&i915->drm, "GuC error state capture buffer lacks spare size: %d < %d (min = %d)\n",
			buffer_size, spare_size, min_size);
}

/*
 * KMD Init time flows:
 * --------------------
 *     --> alloc A: GuC input capture regs lists (registered to GuC via ADS).
 *                  intel_guc_ads acquires the register lists by calling
 *                  intel_guc_capture_list_size and intel_guc_capture_list_get 'n' times,
 *                  where n = 1 for global-reg-list +
 *                            num_engine_classes for class-reg-list +
 *                            num_engine_classes for instance-reg-list
 *                               (since all instances of the same engine-class type
 *                                have an identical engine-instance register-list).
 *                  ADS module also calls separately for PF vs VF.
 *
 *     --> alloc B: GuC output capture buf (registered via guc_init_params(log_param))
 *                  Size = #define CAPTURE_BUFFER_SIZE (warns if on too-small)
 *                  Note2: 'x 3' to hold multiple capture groups
 *
 * GUC Runtime notify capture:
 * --------------------------
 *     --> G2H STATE_CAPTURE_NOTIFICATION
 *                   L--> intel_guc_capture_process
 *                           L--> Loop through B (head..tail) and for each engine instance's
 *                                err-state-captured register-list we find, we alloc 'C':
 *      --> alloc C: A capture-output-node structure that includes misc capture info along
 *                   with 3 register list dumps (global, engine-class and engine-instance)
 *                   This node is created from a pre-allocated list of blank nodes in
 *                   guc->capture->cachelist and populated with the error-capture
 *                   data from GuC and then it's added into guc->capture->outlist linked
 *                   list. This list is used for matchup and printout by i915_gpu_coredump
 *                   and err_print_gt, (when user invokes the error capture sysfs).
 *
 * GUC --> notify context reset:
 * -----------------------------
 *     --> G2H CONTEXT RESET
 *                   L--> guc_handle_context_reset --> i915_capture_error_state
 *                          L--> i915_gpu_coredump(..IS_GUC_CAPTURE) --> gt_record_engines
 *                               --> capture_engine(..IS_GUC_CAPTURE)
 *                               L--> intel_guc_capture_get_matching_node is where
 *                                    detach C from internal linked list and add it into
 *                                    intel_engine_coredump struct (if the context and
 *                                    engine of the event notification matches a node
 *                                    in the link list).
 *
 * User Sysfs / Debugfs
 * --------------------
 *      --> i915_gpu_coredump_copy_to_buffer->
 *                   L--> err_print_to_sgl --> err_print_gt
 *                        L--> error_print_guc_captures
 *                             L--> intel_guc_capture_print_node prints the
 *                                  register lists values of the attached node
 *                                  on the error-engine-dump being reported.
 *                   L--> i915_reset_error_state ... -->__i915_gpu_coredump_free
 *                        L--> ... cleanup_gt -->
 *                             L--> intel_guc_capture_free_node returns the
 *                                  capture-output-node back to the internal
 *                                  cachelist for reuse.
 *
 */

static int guc_capture_buf_cnt(struct __guc_capture_bufstate *buf)
{
	if (buf->wr >= buf->rd)
		return (buf->wr - buf->rd);
	return (buf->size - buf->rd) + buf->wr;
}

static int guc_capture_buf_cnt_to_end(struct __guc_capture_bufstate *buf)
{
	if (buf->rd > buf->wr)
		return (buf->size - buf->rd);
	return (buf->wr - buf->rd);
}

/*
 * GuC's error-capture output is a ring buffer populated in a byte-stream fashion:
 *
 * The GuC Log buffer region for error-capture is managed like a ring buffer.
 * The GuC firmware dumps error capture logs into this ring in a byte-stream flow.
 * Additionally, as per the current and foreseeable future, all packed error-
 * capture output structures are dword aligned.
 *
 * That said, if the GuC firmware is in the midst of writing a structure that is larger
 * than one dword but the tail end of the err-capture buffer-region has lesser space left,
 * we would need to extract that structure one dword at a time straddled across the end,
 * onto the start of the ring.
 *
 * Below function, guc_capture_log_remove_dw is a helper for that. All callers of this
 * function would typically do a straight-up memcpy from the ring contents and will only
 * call this helper if their structure-extraction is straddling across the end of the
 * ring. GuC firmware does not add any padding. The reason for the no-padding is to ease
 * scalability for future expansion of output data types without requiring a redesign
 * of the flow controls.
 */
static int
guc_capture_log_remove_dw(struct intel_guc *guc, struct __guc_capture_bufstate *buf,
			  u32 *dw)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	int tries = 2;
	int avail = 0;
	u32 *src_data;

	if (!guc_capture_buf_cnt(buf))
		return 0;

	while (tries--) {
		avail = guc_capture_buf_cnt_to_end(buf);
		if (avail >= sizeof(u32)) {
			src_data = (u32 *)(buf->data + buf->rd);
			*dw = *src_data;
			buf->rd += 4;
			return 4;
		}
		if (avail)
			drm_dbg(&i915->drm, "GuC-Cap-Logs not dword aligned, skipping.\n");
		buf->rd = 0;
	}

	return 0;
}

static bool
guc_capture_data_extracted(struct __guc_capture_bufstate *b,
			   int size, void *dest)
{
	if (guc_capture_buf_cnt_to_end(b) >= size) {
		memcpy(dest, (b->data + b->rd), size);
		b->rd += size;
		return true;
	}
	return false;
}

static int
guc_capture_log_get_group_hdr(struct intel_guc *guc, struct __guc_capture_bufstate *buf,
			      struct guc_state_capture_group_header_t *ghdr)
{
	int read = 0;
	int fullsize = sizeof(struct guc_state_capture_group_header_t);

	if (fullsize > guc_capture_buf_cnt(buf))
		return -1;

	if (guc_capture_data_extracted(buf, fullsize, (void *)ghdr))
		return 0;

	read += guc_capture_log_remove_dw(guc, buf, &ghdr->owner);
	read += guc_capture_log_remove_dw(guc, buf, &ghdr->info);
	if (read != fullsize)
		return -1;

	return 0;
}

static int
guc_capture_log_get_data_hdr(struct intel_guc *guc, struct __guc_capture_bufstate *buf,
			     struct guc_state_capture_header_t *hdr)
{
	int read = 0;
	int fullsize = sizeof(struct guc_state_capture_header_t);

	if (fullsize > guc_capture_buf_cnt(buf))
		return -1;

	if (guc_capture_data_extracted(buf, fullsize, (void *)hdr))
		return 0;

	read += guc_capture_log_remove_dw(guc, buf, &hdr->owner);
	read += guc_capture_log_remove_dw(guc, buf, &hdr->info);
	read += guc_capture_log_remove_dw(guc, buf, &hdr->lrca);
	read += guc_capture_log_remove_dw(guc, buf, &hdr->guc_id);
	read += guc_capture_log_remove_dw(guc, buf, &hdr->num_mmios);
	if (read != fullsize)
		return -1;

	return 0;
}

static int
guc_capture_log_get_register(struct intel_guc *guc, struct __guc_capture_bufstate *buf,
			     struct guc_mmio_reg *reg)
{
	int read = 0;
	int fullsize = sizeof(struct guc_mmio_reg);

	if (fullsize > guc_capture_buf_cnt(buf))
		return -1;

	if (guc_capture_data_extracted(buf, fullsize, (void *)reg))
		return 0;

	read += guc_capture_log_remove_dw(guc, buf, &reg->offset);
	read += guc_capture_log_remove_dw(guc, buf, &reg->value);
	read += guc_capture_log_remove_dw(guc, buf, &reg->flags);
	read += guc_capture_log_remove_dw(guc, buf, &reg->mask);
	if (read != fullsize)
		return -1;

	return 0;
}

static void
guc_capture_delete_one_node(struct intel_guc *guc, struct __guc_capture_parsed_output *node)
{
	int i;

	for (i = 0; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i)
		kfree(node->reginfo[i].regs);
	list_del(&node->link);
	kfree(node);
}

static void
guc_capture_delete_prealloc_nodes(struct intel_guc *guc)
{
	struct __guc_capture_parsed_output *n, *ntmp;

	/*
	 * NOTE: At the end of driver operation, we must assume that we
	 * have prealloc nodes in both the cachelist as well as outlist
	 * if unclaimed error capture events occurred prior to shutdown.
	 */
	list_for_each_entry_safe(n, ntmp, &guc->capture->outlist, link)
		guc_capture_delete_one_node(guc, n);

	list_for_each_entry_safe(n, ntmp, &guc->capture->cachelist, link)
		guc_capture_delete_one_node(guc, n);
}

static void
guc_capture_add_node_to_list(struct __guc_capture_parsed_output *node,
			     struct list_head *list)
{
	list_add_tail(&node->link, list);
}

static void
guc_capture_add_node_to_outlist(struct intel_guc_state_capture *gc,
				struct __guc_capture_parsed_output *node)
{
	guc_capture_add_node_to_list(node, &gc->outlist);
}

static void
guc_capture_add_node_to_cachelist(struct intel_guc_state_capture *gc,
				  struct __guc_capture_parsed_output *node)
{
	guc_capture_add_node_to_list(node, &gc->cachelist);
}

static void
guc_capture_init_node(struct intel_guc *guc, struct __guc_capture_parsed_output *node)
{
	struct guc_mmio_reg *tmp[GUC_CAPTURE_LIST_TYPE_MAX];
	int i;

	for (i = 0; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
		tmp[i] = node->reginfo[i].regs;
		memset(tmp[i], 0, sizeof(struct guc_mmio_reg) *
		       guc->capture->max_mmio_per_node);
	}
	memset(node, 0, sizeof(*node));
	for (i = 0; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i)
		node->reginfo[i].regs = tmp[i];

	INIT_LIST_HEAD(&node->link);
}

static struct __guc_capture_parsed_output *
guc_capture_get_prealloc_node(struct intel_guc *guc)
{
	struct __guc_capture_parsed_output *found = NULL;

	if (!list_empty(&guc->capture->cachelist)) {
		struct __guc_capture_parsed_output *n, *ntmp;

		/* get first avail node from the cache list */
		list_for_each_entry_safe(n, ntmp, &guc->capture->cachelist, link) {
			found = n;
			list_del(&n->link);
			break;
		}
	} else {
		struct __guc_capture_parsed_output *n, *ntmp;

		/* traverse down and steal back the oldest node already allocated */
		list_for_each_entry_safe(n, ntmp, &guc->capture->outlist, link) {
			found = n;
		}
		if (found)
			list_del(&found->link);
	}
	if (found)
		guc_capture_init_node(guc, found);

	return found;
}

static struct __guc_capture_parsed_output *
guc_capture_alloc_one_node(struct intel_guc *guc)
{
	struct __guc_capture_parsed_output *new;
	int i;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	for (i = 0; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
		new->reginfo[i].regs = kcalloc(guc->capture->max_mmio_per_node,
					       sizeof(struct guc_mmio_reg), GFP_KERNEL);
		if (!new->reginfo[i].regs) {
			while (i)
				kfree(new->reginfo[--i].regs);
			kfree(new);
			return NULL;
		}
	}
	guc_capture_init_node(guc, new);

	return new;
}

static struct __guc_capture_parsed_output *
guc_capture_clone_node(struct intel_guc *guc, struct __guc_capture_parsed_output *original,
		       u32 keep_reglist_mask)
{
	struct __guc_capture_parsed_output *new;
	int i;

	new = guc_capture_get_prealloc_node(guc);
	if (!new)
		return NULL;
	if (!original)
		return new;

	new->is_partial = original->is_partial;

	/* copy reg-lists that we want to clone */
	for (i = 0; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
		if (keep_reglist_mask & BIT(i)) {
			GEM_BUG_ON(original->reginfo[i].num_regs  >
				   guc->capture->max_mmio_per_node);

			memcpy(new->reginfo[i].regs, original->reginfo[i].regs,
			       original->reginfo[i].num_regs * sizeof(struct guc_mmio_reg));

			new->reginfo[i].num_regs = original->reginfo[i].num_regs;
			new->reginfo[i].vfid  = original->reginfo[i].vfid;

			if (i == GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS) {
				new->eng_class = original->eng_class;
			} else if (i == GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE) {
				new->eng_inst = original->eng_inst;
				new->guc_id = original->guc_id;
				new->lrca = original->lrca;
			}
		}
	}

	return new;
}

static void
__guc_capture_create_prealloc_nodes(struct intel_guc *guc)
{
	struct __guc_capture_parsed_output *node = NULL;
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	int i;

	for (i = 0; i < PREALLOC_NODES_MAX_COUNT; ++i) {
		node = guc_capture_alloc_one_node(guc);
		if (!node) {
			drm_warn(&i915->drm, "GuC Capture pre-alloc-cache failure\n");
			/* dont free the priors, use what we got and cleanup at shutdown */
			return;
		}
		guc_capture_add_node_to_cachelist(guc->capture, node);
	}
}

static int
guc_get_max_reglist_count(struct intel_guc *guc)
{
	int i, j, k, tmp, maxregcount = 0;

	for (i = 0; i < GUC_CAPTURE_LIST_INDEX_MAX; ++i) {
		for (j = 0; j < GUC_CAPTURE_LIST_TYPE_MAX; ++j) {
			for (k = 0; k < GUC_MAX_ENGINE_CLASSES; ++k) {
				if (j == GUC_CAPTURE_LIST_TYPE_GLOBAL && k > 0)
					continue;

				tmp = guc_cap_list_num_regs(guc->capture, i, j, k);
				if (tmp > maxregcount)
					maxregcount = tmp;
			}
		}
	}
	if (!maxregcount)
		maxregcount = PREALLOC_NODES_DEFAULT_NUMREGS;

	return maxregcount;
}

static void
guc_capture_create_prealloc_nodes(struct intel_guc *guc)
{
	/* skip if we've already done the pre-alloc */
	if (guc->capture->max_mmio_per_node)
		return;

	guc->capture->max_mmio_per_node = guc_get_max_reglist_count(guc);
	__guc_capture_create_prealloc_nodes(guc);
}

static int
guc_capture_extract_reglists(struct intel_guc *guc, struct __guc_capture_bufstate *buf)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct guc_state_capture_group_header_t ghdr = {0};
	struct guc_state_capture_header_t hdr = {0};
	struct __guc_capture_parsed_output *node = NULL;
	struct guc_mmio_reg *regs = NULL;
	int i, numlists, numregs, ret = 0;
	enum guc_capture_type datatype;
	struct guc_mmio_reg tmp;
	bool is_partial = false;

	i = guc_capture_buf_cnt(buf);
	if (!i)
		return -ENODATA;
	if (i % sizeof(u32)) {
		drm_warn(&i915->drm, "GuC Capture new entries unaligned\n");
		ret = -EIO;
		goto bailout;
	}

	/* first get the capture group header */
	if (guc_capture_log_get_group_hdr(guc, buf, &ghdr)) {
		ret = -EIO;
		goto bailout;
	}
	/*
	 * we would typically expect a layout as below where n would be expected to be
	 * anywhere between 3 to n where n > 3 if we are seeing multiple dependent engine
	 * instances being reset together.
	 * ____________________________________________
	 * | Capture Group                            |
	 * | ________________________________________ |
	 * | | Capture Group Header:                | |
	 * | |  - num_captures = 5                  | |
	 * | |______________________________________| |
	 * | ________________________________________ |
	 * | | Capture1:                            | |
	 * | |  Hdr: GLOBAL, numregs=a              | |
	 * | | ____________________________________ | |
	 * | | | Reglist                          | | |
	 * | | | - reg1, reg2, ... rega           | | |
	 * | | |__________________________________| | |
	 * | |______________________________________| |
	 * | ________________________________________ |
	 * | | Capture2:                            | |
	 * | |  Hdr: CLASS=RENDER/COMPUTE, numregs=b| |
	 * | | ____________________________________ | |
	 * | | | Reglist                          | | |
	 * | | | - reg1, reg2, ... regb           | | |
	 * | | |__________________________________| | |
	 * | |______________________________________| |
	 * | ________________________________________ |
	 * | | Capture3:                            | |
	 * | |  Hdr: INSTANCE=RCS, numregs=c        | |
	 * | | ____________________________________ | |
	 * | | | Reglist                          | | |
	 * | | | - reg1, reg2, ... regc           | | |
	 * | | |__________________________________| | |
	 * | |______________________________________| |
	 * | ________________________________________ |
	 * | | Capture4:                            | |
	 * | |  Hdr: CLASS=RENDER/COMPUTE, numregs=d| |
	 * | | ____________________________________ | |
	 * | | | Reglist                          | | |
	 * | | | - reg1, reg2, ... regd           | | |
	 * | | |__________________________________| | |
	 * | |______________________________________| |
	 * | ________________________________________ |
	 * | | Capture5:                            | |
	 * | |  Hdr: INSTANCE=CCS0, numregs=e       | |
	 * | | ____________________________________ | |
	 * | | | Reglist                          | | |
	 * | | | - reg1, reg2, ... rege           | | |
	 * | | |__________________________________| | |
	 * | |______________________________________| |
	 * |__________________________________________|
	 */
	is_partial = FIELD_GET(CAP_GRP_HDR_CAPTURE_TYPE, ghdr.info);
	numlists = FIELD_GET(CAP_GRP_HDR_NUM_CAPTURES, ghdr.info);

	while (numlists--) {
		if (guc_capture_log_get_data_hdr(guc, buf, &hdr)) {
			ret = -EIO;
			break;
		}

		datatype = FIELD_GET(CAP_HDR_CAPTURE_TYPE, hdr.info);
		if (datatype > GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE) {
			/* unknown capture type - skip over to next capture set */
			numregs = FIELD_GET(CAP_HDR_NUM_MMIOS, hdr.num_mmios);
			while (numregs--) {
				if (guc_capture_log_get_register(guc, buf, &tmp)) {
					ret = -EIO;
					break;
				}
			}
			continue;
		} else if (node) {
			/*
			 * Based on the current capture type and what we have so far,
			 * decide if we should add the current node into the internal
			 * linked list for match-up when i915_gpu_coredump calls later
			 * (and alloc a blank node for the next set of reglists)
			 * or continue with the same node or clone the current node
			 * but only retain the global or class registers (such as the
			 * case of dependent engine resets).
			 */
			if (datatype == GUC_CAPTURE_LIST_TYPE_GLOBAL) {
				guc_capture_add_node_to_outlist(guc->capture, node);
				node = NULL;
			} else if (datatype == GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS &&
				   node->reginfo[GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS].num_regs) {
				/* Add to list, clone node and duplicate global list */
				guc_capture_add_node_to_outlist(guc->capture, node);
				node = guc_capture_clone_node(guc, node,
							      GCAP_PARSED_REGLIST_INDEX_GLOBAL);
			} else if (datatype == GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE &&
				   node->reginfo[GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE].num_regs) {
				/* Add to list, clone node and duplicate global + class lists */
				guc_capture_add_node_to_outlist(guc->capture, node);
				node = guc_capture_clone_node(guc, node,
							      (GCAP_PARSED_REGLIST_INDEX_GLOBAL |
							      GCAP_PARSED_REGLIST_INDEX_ENGCLASS));
			}
		}

		if (!node) {
			node = guc_capture_get_prealloc_node(guc);
			if (!node) {
				ret = -ENOMEM;
				break;
			}
			if (datatype != GUC_CAPTURE_LIST_TYPE_GLOBAL)
				drm_dbg(&i915->drm, "GuC Capture missing global dump: %08x!\n",
					datatype);
		}
		node->is_partial = is_partial;
		node->reginfo[datatype].vfid = FIELD_GET(CAP_HDR_CAPTURE_VFID, hdr.owner);
		switch (datatype) {
		case GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE:
			node->eng_class = FIELD_GET(CAP_HDR_ENGINE_CLASS, hdr.info);
			node->eng_inst = FIELD_GET(CAP_HDR_ENGINE_INSTANCE, hdr.info);
			node->lrca = hdr.lrca;
			node->guc_id = hdr.guc_id;
			break;
		case GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS:
			node->eng_class = FIELD_GET(CAP_HDR_ENGINE_CLASS, hdr.info);
			break;
		default:
			break;
		}

		numregs = FIELD_GET(CAP_HDR_NUM_MMIOS, hdr.num_mmios);
		if (numregs > guc->capture->max_mmio_per_node) {
			drm_dbg(&i915->drm, "GuC Capture list extraction clipped by prealloc!\n");
			numregs = guc->capture->max_mmio_per_node;
		}
		node->reginfo[datatype].num_regs = numregs;
		regs = node->reginfo[datatype].regs;
		i = 0;
		while (numregs--) {
			if (guc_capture_log_get_register(guc, buf, &regs[i++])) {
				ret = -EIO;
				break;
			}
		}
	}

bailout:
	if (node) {
		/* If we have data, add to linked list for match-up when i915_gpu_coredump calls */
		for (i = GUC_CAPTURE_LIST_TYPE_GLOBAL; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
			if (node->reginfo[i].regs) {
				guc_capture_add_node_to_outlist(guc->capture, node);
				node = NULL;
				break;
			}
		}
		if (node) /* else return it back to cache list */
			guc_capture_add_node_to_cachelist(guc->capture, node);
	}
	return ret;
}

static int __guc_capture_flushlog_complete(struct intel_guc *guc)
{
	u32 action[] = {
		INTEL_GUC_ACTION_LOG_BUFFER_FILE_FLUSH_COMPLETE,
		GUC_CAPTURE_LOG_BUFFER
	};

	return intel_guc_send_nb(guc, action, ARRAY_SIZE(action), 0);

}

static void __guc_capture_process_output(struct intel_guc *guc)
{
	unsigned int buffer_size, read_offset, write_offset, full_count;
	struct intel_uc *uc = container_of(guc, typeof(*uc), guc);
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct guc_log_buffer_state log_buf_state_local;
	struct guc_log_buffer_state *log_buf_state;
	struct __guc_capture_bufstate buf;
	void *src_data = NULL;
	bool new_overflow;
	int ret;

	log_buf_state = guc->log.buf_addr +
			(sizeof(struct guc_log_buffer_state) * GUC_CAPTURE_LOG_BUFFER);
	src_data = guc->log.buf_addr +
		   intel_guc_get_log_buffer_offset(&guc->log, GUC_CAPTURE_LOG_BUFFER);

	/*
	 * Make a copy of the state structure, inside GuC log buffer
	 * (which is uncached mapped), on the stack to avoid reading
	 * from it multiple times.
	 */
	memcpy(&log_buf_state_local, log_buf_state, sizeof(struct guc_log_buffer_state));
	buffer_size = intel_guc_get_log_buffer_size(&guc->log, GUC_CAPTURE_LOG_BUFFER);
	read_offset = log_buf_state_local.read_ptr;
	write_offset = log_buf_state_local.sampled_write_ptr;
	full_count = log_buf_state_local.buffer_full_cnt;

	/* Bookkeeping stuff */
	guc->log.stats[GUC_CAPTURE_LOG_BUFFER].flush += log_buf_state_local.flush_to_file;
	new_overflow = intel_guc_check_log_buf_overflow(&guc->log, GUC_CAPTURE_LOG_BUFFER,
							full_count);

	/* Now copy the actual logs. */
	if (unlikely(new_overflow)) {
		/* copy the whole buffer in case of overflow */
		read_offset = 0;
		write_offset = buffer_size;
	} else if (unlikely((read_offset > buffer_size) ||
			(write_offset > buffer_size))) {
		drm_err(&i915->drm, "invalid GuC log capture buffer state!\n");
		/* copy whole buffer as offsets are unreliable */
		read_offset = 0;
		write_offset = buffer_size;
	}

	buf.size = buffer_size;
	buf.rd = read_offset;
	buf.wr = write_offset;
	buf.data = src_data;

	if (!uc->reset_in_progress) {
		do {
			ret = guc_capture_extract_reglists(guc, &buf);
		} while (ret >= 0);
	}

	/* Update the state of log buffer err-cap state */
	log_buf_state->read_ptr = write_offset;
	log_buf_state->flush_to_file = 0;
	__guc_capture_flushlog_complete(guc);
}

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)

static const char *
guc_capture_reg_to_str(const struct intel_guc *guc, u32 owner, u32 type,
		       u32 class, u32 id, u32 offset, u32 *is_ext)
{
	const struct __guc_mmio_reg_descr_group *reglists = guc->capture->reglists;
	struct __guc_mmio_reg_descr_group *extlists = guc->capture->extlists;
	const struct __guc_mmio_reg_descr_group *match;
	struct __guc_mmio_reg_descr_group *matchext;
	int j;

	*is_ext = 0;
	if (!reglists)
		return NULL;

	match = guc_capture_get_one_list(reglists, owner, type, id);
	if (!match)
		return NULL;

	for (j = 0; j < match->num_regs; ++j) {
		if (offset == match->list[j].reg.reg)
			return match->list[j].regname;
	}
	if (extlists) {
		matchext = guc_capture_get_one_ext_list(extlists, owner, type, id);
		if (!matchext)
			return NULL;
		for (j = 0; j < matchext->num_regs; ++j) {
			if (offset == matchext->extlist[j].reg.reg) {
				*is_ext = 1;
				return matchext->extlist[j].regname;
			}
		}
	}

	return NULL;
}

#define GCAP_PRINT_INTEL_ENG_INFO(ebuf, eng) \
	do { \
		i915_error_printf(ebuf, "    i915-Eng-Name: %s command stream\n", \
				  (eng)->name); \
		i915_error_printf(ebuf, "    i915-Eng-Inst-Class: 0x%02x\n", (eng)->class); \
		i915_error_printf(ebuf, "    i915-Eng-Inst-Id: 0x%02x\n", (eng)->instance); \
		i915_error_printf(ebuf, "    i915-Eng-LogicalMask: 0x%08x\n", \
				  (eng)->logical_mask); \
	} while (0)

#define GCAP_PRINT_GUC_INST_INFO(ebuf, node) \
	do { \
		i915_error_printf(ebuf, "    GuC-Engine-Inst-Id: 0x%08x\n", \
				  (node)->eng_inst); \
		i915_error_printf(ebuf, "    GuC-Context-Id: 0x%08x\n", (node)->guc_id); \
		i915_error_printf(ebuf, "    LRCA: 0x%08x\n", (node)->lrca); \
	} while (0)

int intel_guc_capture_print_engine_node(struct drm_i915_error_state_buf *ebuf,
					const struct intel_engine_coredump *ee)
{
	const char *grptype[GUC_STATE_CAPTURE_GROUP_TYPE_MAX] = {
		"full-capture",
		"partial-capture"
	};
	const char *datatype[GUC_CAPTURE_LIST_TYPE_MAX] = {
		"Global",
		"Engine-Class",
		"Engine-Instance"
	};
	struct intel_guc_state_capture *cap;
	struct __guc_capture_parsed_output *node;
	struct intel_engine_cs *eng;
	struct guc_mmio_reg *regs;
	struct intel_guc *guc;
	const char *str;
	int numregs, i, j;
	u32 is_ext;

	if (!ebuf || !ee)
		return -EINVAL;
	cap = ee->guc_capture;
	if (!cap || !ee->engine)
		return -ENODEV;

	guc = &ee->engine->gt->uc.guc;

	i915_error_printf(ebuf, "global --- GuC Error Capture on %s command stream:\n",
			  ee->engine->name);

	node = ee->guc_capture_node;
	if (!node) {
		i915_error_printf(ebuf, "  No matching ee-node\n");
		return 0;
	}

	i915_error_printf(ebuf, "Coverage:  %s\n", grptype[node->is_partial]);

	for (i = GUC_CAPTURE_LIST_TYPE_GLOBAL; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
		i915_error_printf(ebuf, "  RegListType: %s\n",
				  datatype[i % GUC_CAPTURE_LIST_TYPE_MAX]);
		i915_error_printf(ebuf, "    Owner-Id: %d\n", node->reginfo[i].vfid);

		switch (i) {
		case GUC_CAPTURE_LIST_TYPE_GLOBAL:
		default:
			break;
		case GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS:
			i915_error_printf(ebuf, "    GuC-Eng-Class: %d\n", node->eng_class);
			i915_error_printf(ebuf, "    i915-Eng-Class: %d\n",
					  guc_class_to_engine_class(node->eng_class));
			break;
		case GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE:
			eng = intel_guc_lookup_engine(guc, node->eng_class, node->eng_inst);
			if (eng)
				GCAP_PRINT_INTEL_ENG_INFO(ebuf, eng);
			else
				i915_error_printf(ebuf, "    i915-Eng-Lookup Fail!\n");
			GCAP_PRINT_GUC_INST_INFO(ebuf, node);
			break;
		}

		numregs = node->reginfo[i].num_regs;
		i915_error_printf(ebuf, "    NumRegs: %d\n", numregs);
		j = 0;
		while (numregs--) {
			regs = node->reginfo[i].regs;
			str = guc_capture_reg_to_str(guc, GUC_CAPTURE_LIST_INDEX_PF, i,
						     node->eng_class, 0, regs[j].offset, &is_ext);
			if (!str)
				i915_error_printf(ebuf, "      REG-0x%08x", regs[j].offset);
			else
				i915_error_printf(ebuf, "      %s", str);
			if (is_ext)
				i915_error_printf(ebuf, "[%ld][%ld]",
					FIELD_GET(GUC_REGSET_STEERING_GROUP, regs[j].flags),
					FIELD_GET(GUC_REGSET_STEERING_INSTANCE, regs[j].flags));
			i915_error_printf(ebuf, ":  0x%08x\n", regs[j].value);
			++j;
		}
	}
	return 0;
}

#endif //CONFIG_DRM_I915_CAPTURE_ERROR

static void guc_capture_find_ecode(struct intel_engine_coredump *ee)
{
	struct gcap_reg_list_info *reginfo;
	struct guc_mmio_reg *regs;
	i915_reg_t reg_ipehr = RING_IPEHR(0);
	i915_reg_t reg_instdone = RING_INSTDONE(0);
	int i;

	if (!ee->guc_capture_node)
		return;

	reginfo = ee->guc_capture_node->reginfo + GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE;
	regs = reginfo->regs;
	for (i = 0; i < reginfo->num_regs; i++) {
		if (regs[i].offset == reg_ipehr.reg)
			ee->ipehr = regs[i].value;
		else if (regs[i].offset == reg_instdone.reg)
			ee->instdone.instdone = regs[i].value;
	}
}

void intel_guc_capture_free_node(struct intel_engine_coredump *ee)
{
	if (!ee || !ee->guc_capture_node)
		return;

	guc_capture_add_node_to_cachelist(ee->guc_capture, ee->guc_capture_node);
	ee->guc_capture = NULL;
	ee->guc_capture_node = NULL;
}

void intel_guc_capture_get_matching_node(struct intel_gt *gt,
					 struct intel_engine_coredump *ee,
					 struct intel_context *ce)
{
	struct __guc_capture_parsed_output *n, *ntmp;
	struct drm_i915_private *i915;
	struct intel_guc *guc;

	if (!gt || !ee || !ce)
		return;

	i915 = gt->i915;
	guc = &gt->uc.guc;
	if (!guc->capture)
		return;

	GEM_BUG_ON(ee->guc_capture_node);
	/*
	 * Look for a matching GuC reported error capture node from
	 * the internal output link-list based on lrca, guc-id and engine
	 * identification.
	 */
	list_for_each_entry_safe(n, ntmp, &guc->capture->outlist, link) {
		if (n->eng_inst == GUC_ID_TO_ENGINE_INSTANCE(ee->engine->guc_id) &&
		    n->eng_class == GUC_ID_TO_ENGINE_CLASS(ee->engine->guc_id) &&
		    n->guc_id && n->guc_id == ce->guc_id.id &&
		    (n->lrca & CTX_GTT_ADDRESS_MASK) && (n->lrca & CTX_GTT_ADDRESS_MASK) ==
		    (ce->lrc.lrca & CTX_GTT_ADDRESS_MASK)) {
			list_del(&n->link);
			ee->guc_capture_node = n;
			ee->guc_capture = guc->capture;
			guc_capture_find_ecode(ee);
			return;
		}
	}
	drm_dbg(&i915->drm, "GuC capture can't match ee to node\n");
}

void intel_guc_capture_process(struct intel_guc *guc)
{
	if (guc->capture)
		__guc_capture_process_output(guc);
}

static void
guc_capture_free_ads_cache(struct intel_guc_state_capture *gc)
{
	int i, j, k;
	struct __guc_capture_ads_cache *cache;

	for (i = 0; i < GUC_CAPTURE_LIST_INDEX_MAX; ++i) {
		for (j = 0; j < GUC_CAPTURE_LIST_TYPE_MAX; ++j) {
			for (k = 0; k < GUC_MAX_ENGINE_CLASSES; ++k) {
				cache = &gc->ads_cache[i][j][k];
				if (cache->is_valid)
					kfree(cache->ptr);
			}
		}
	}
	kfree(gc->ads_null_cache);
}

void intel_guc_capture_destroy(struct intel_guc *guc)
{
	if (!guc->capture)
		return;

	guc_capture_free_ads_cache(guc->capture);

	guc_capture_delete_prealloc_nodes(guc);

	guc_capture_free_extlists(guc->capture->extlists);
	kfree(guc->capture->extlists);

	kfree(guc->capture);
	guc->capture = NULL;
}

int intel_guc_capture_init(struct intel_guc *guc)
{
	guc->capture = kzalloc(sizeof(*guc->capture), GFP_KERNEL);
	if (!guc->capture)
		return -ENOMEM;

	guc->capture->reglists = guc_capture_get_device_reglist(guc);

	INIT_LIST_HEAD(&guc->capture->outlist);
	INIT_LIST_HEAD(&guc->capture->cachelist);

	check_guc_capture_size(guc);

	return 0;
}

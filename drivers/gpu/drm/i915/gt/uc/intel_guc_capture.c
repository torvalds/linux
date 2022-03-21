// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2022 Intel Corporation
 */

#include <linux/types.h>

#include <drm/drm_print.h>

#include "gt/intel_engine_regs.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_regs.h"
#include "guc_capture_fwif.h"
#include "intel_guc_capture.h"
#include "intel_guc_fwif.h"
#include "i915_drv.h"
#include "i915_memcpy.h"
#include "i915_reg.h"

/*
 * Define all device tables of GuC error capture register lists
 * NOTE: For engine-registers, GuC only needs the register offsets
 *       from the engine-mmio-base
 */
#define COMMON_GEN12BASE_GLOBAL \
	{ GEN12_FAULT_TLB_DATA0,    0,      0, "GEN12_FAULT_TLB_DATA0" }, \
	{ GEN12_FAULT_TLB_DATA1,    0,      0, "GEN12_FAULT_TLB_DATA1" }, \
	{ FORCEWAKE_MT,             0,      0, "FORCEWAKE" }, \
	{ GEN12_AUX_ERR_DBG,        0,      0, "AUX_ERR_DBG" }, \
	{ GEN12_GAM_DONE,           0,      0, "GAM_DONE" }, \
	{ GEN12_RING_FAULT_REG,     0,      0, "FAULT_REG" }

#define COMMON_GEN12BASE_ENGINE_INSTANCE \
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

#define COMMON_GEN12BASE_HAS_EU \
	{ EIR,                      0,      0, "EIR" }

#define COMMON_GEN12BASE_RENDER \
	{ GEN7_SC_INSTDONE,         0,      0, "GEN7_SC_INSTDONE" }, \
	{ GEN12_SC_INSTDONE_EXTRA,  0,      0, "GEN12_SC_INSTDONE_EXTRA" }, \
	{ GEN12_SC_INSTDONE_EXTRA2, 0,      0, "GEN12_SC_INSTDONE_EXTRA2" }

#define COMMON_GEN12BASE_VEC \
	{ GEN12_SFC_DONE(0),        0,      0, "SFC_DONE[0]" }, \
	{ GEN12_SFC_DONE(1),        0,      0, "SFC_DONE[1]" }, \
	{ GEN12_SFC_DONE(2),        0,      0, "SFC_DONE[2]" }, \
	{ GEN12_SFC_DONE(3),        0,      0, "SFC_DONE[3]" }

/* XE_LPD - Global */
static const struct __guc_mmio_reg_descr xe_lpd_global_regs[] = {
	COMMON_GEN12BASE_GLOBAL,
};

/* XE_LPD - Render / Compute Per-Class */
static const struct __guc_mmio_reg_descr xe_lpd_rc_class_regs[] = {
	COMMON_GEN12BASE_HAS_EU,
	COMMON_GEN12BASE_RENDER,
};

/* XE_LPD - Render / Compute Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_rc_inst_regs[] = {
	COMMON_GEN12BASE_ENGINE_INSTANCE,
};

/* XE_LPD - Media Decode/Encode Per-Class */
static const struct __guc_mmio_reg_descr xe_lpd_vd_class_regs[] = {
	COMMON_GEN12BASE_ENGINE_INSTANCE,
};

/* XE_LPD - Media Decode/Encode Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_vd_inst_regs[] = {
	COMMON_GEN12BASE_ENGINE_INSTANCE,
};

/* XE_LPD - Video Enhancement Per-Class */
static const struct __guc_mmio_reg_descr xe_lpd_vec_class_regs[] = {
	COMMON_GEN12BASE_VEC,
};

/* XE_LPD - Video Enhancement Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_vec_inst_regs[] = {
	COMMON_GEN12BASE_ENGINE_INSTANCE,
};

/* XE_LPD - Blitter Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_blt_inst_regs[] = {
	COMMON_GEN12BASE_ENGINE_INSTANCE,
};

/* XE_LPD - Blitter Per-Class */
/* XE_LPD - Media Decode/Encode Per-Class */
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
static const struct __guc_mmio_reg_descr_group xe_lpd_lists[] = {
	MAKE_REGLIST(xe_lpd_global_regs, PF, GLOBAL, 0),
	MAKE_REGLIST(xe_lpd_rc_class_regs, PF, ENGINE_CLASS, GUC_RENDER_CLASS),
	MAKE_REGLIST(xe_lpd_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_RENDER_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vd_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vec_class_regs, PF, ENGINE_CLASS, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(xe_lpd_vec_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_BLITTER_CLASS),
	MAKE_REGLIST(xe_lpd_blt_inst_regs, PF, ENGINE_INSTANCE, GUC_BLITTER_CLASS),
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
	i915_reg_t reg;
};

static const struct __ext_steer_reg xe_extregs[] = {
	{"GEN7_SAMPLER_INSTDONE", GEN7_SAMPLER_INSTDONE},
	{"GEN7_ROW_INSTDONE", GEN7_ROW_INSTDONE}
};

static void __fill_ext_reg(struct __guc_mmio_reg_descr *ext,
			   const struct __ext_steer_reg *extlist,
			   int slice_id, int subslice_id)
{
	ext->reg = extlist->reg;
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
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	int slice, subslice, i, num_steer_regs, num_tot_regs = 0;
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
	for_each_instdone_slice_subslice(i915, sseu, slice, subslice)
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
	for_each_instdone_slice_subslice(i915, sseu, slice, subslice) {
		for (i = 0; i < num_steer_regs; ++i) {
			__fill_ext_reg(extarray, &xe_extregs[i], slice, subslice);
			++extarray;
		}
	}

	guc->capture->extlists = extlists;
}

static const struct __guc_mmio_reg_descr_group *
guc_capture_get_device_reglist(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	if (IS_TIGERLAKE(i915) || IS_ROCKETLAKE(i915) ||
	    IS_ALDERLAKE_S(i915) || IS_ALDERLAKE_P(i915)) {
		/*
		 * For certain engine classes, there are slice and subslice
		 * level registers requiring steering. We allocate and populate
		 * these at init time based on hw config add it as an extension
		 * list at the end of the pre-populated render list.
		 */
		guc_capture_alloc_steered_lists_xe_lpd(guc, xe_lpd_lists);
		return xe_lpd_lists;
	}

	drm_warn(&i915->drm, "No GuC-capture register lists\n");

	return NULL;
}

static const char *
__stringify_owner(u32 owner)
{
	switch (owner) {
	case GUC_CAPTURE_LIST_INDEX_PF:
		return "PF";
	case GUC_CAPTURE_LIST_INDEX_VF:
		return "VF";
	default:
		return "unknown";
	}

	return "";
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
		return "unknown";
	}

	return "";
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
	default:
		return "unknown";
	}

	return "";
}

static void
guc_capture_warn_with_list_info(struct drm_i915_private *i915, char *msg,
				u32 owner, u32 type, u32 classid)
{
	if (type == GUC_CAPTURE_LIST_TYPE_GLOBAL)
		drm_dbg(&i915->drm, "GuC-capture: %s for %s %s-Registers.\n", msg,
			__stringify_owner(owner), __stringify_type(type));
	else
		drm_dbg(&i915->drm, "GuC-capture: %s for %s %s-Registers on %s-Engine\n", msg,
			__stringify_owner(owner), __stringify_type(type),
			__stringify_engclass(classid));
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
	if (!match) {
		guc_capture_warn_with_list_info(i915, "Missing register list init", owner, type,
						classid);
		return -ENODATA;
	}

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

int
intel_guc_capture_getlistsize(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
			      size_t *size)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct intel_guc_state_capture *gc = guc->capture;
	struct __guc_capture_ads_cache *cache = &gc->ads_cache[owner][type][classid];
	int num_regs;

	if (!gc->reglists)
		return -ENODEV;

	if (cache->is_valid) {
		*size = cache->size;
		return cache->status;
	}

	num_regs = guc_cap_list_num_regs(gc, owner, type, classid);
	if (!num_regs) {
		guc_capture_warn_with_list_info(i915, "Missing register list size",
						owner, type, classid);
		return -ENODATA;
	}

	*size = PAGE_ALIGN((sizeof(struct guc_debug_capture_list)) +
			   (num_regs * sizeof(struct guc_mmio_reg)));

	return 0;
}

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

	kfree(guc->capture);
	guc->capture = NULL;

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

	return 0;
}

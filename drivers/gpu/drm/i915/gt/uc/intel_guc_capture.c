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
/* XE_LPD - Global */
static const struct __guc_mmio_reg_descr xe_lpd_global_regs[] = {
	{ GEN12_RING_FAULT_REG,     0,      0, "GEN12_RING_FAULT_REG" }
};

/* XE_LPD - Render / Compute Per-Class */
static const struct __guc_mmio_reg_descr xe_lpd_rc_class_regs[] = {
	{ EIR,                      0,      0, "EIR" }
};

/* XE_LPD - Render / Compute Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_rc_inst_regs[] = {
	{ RING_HEAD(0),             0,      0, "RING_HEAD" },
	{ RING_TAIL(0),             0,      0, "RING_TAIL" },
};

/* XE_LPD - Media Decode/Encode Per-Class */
static const struct __guc_mmio_reg_descr xe_lpd_vd_class_regs[] = {
};

/* XE_LPD - Media Decode/Encode Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_vd_inst_regs[] = {
	{ RING_HEAD(0),             0,      0, "RING_HEAD" },
	{ RING_TAIL(0),             0,      0, "RING_TAIL" },
};

/* XE_LPD - Video Enhancement Per-Class */
static const struct __guc_mmio_reg_descr xe_lpd_vec_class_regs[] = {
};

/* XE_LPD - Video Enhancement Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lpd_vec_inst_regs[] = {
	{ RING_HEAD(0),             0,      0, "RING_HEAD" },
	{ RING_TAIL(0),             0,      0, "RING_TAIL" },
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
	}

/* List of lists */
static const struct __guc_mmio_reg_descr_group xe_lpd_lists[] = {
	MAKE_REGLIST(xe_lpd_global_regs, PF, GLOBAL, 0),
	MAKE_REGLIST(xe_lpd_rc_class_regs, PF, ENGINE_CLASS, GUC_RENDER_CLASS),
	MAKE_REGLIST(xe_lpd_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_RENDER_CLASS),
	MAKE_REGLIST(xe_lpd_vd_class_regs, PF, ENGINE_CLASS, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vd_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vec_class_regs, PF, ENGINE_CLASS, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(xe_lpd_vec_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEOENHANCE_CLASS),
	{}
};

static const struct __guc_mmio_reg_descr_group *
guc_capture_get_device_reglist(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	if (IS_TIGERLAKE(i915) || IS_ROCKETLAKE(i915) ||
	    IS_ALDERLAKE_S(i915) || IS_ALDERLAKE_P(i915)) {
		return xe_lpd_lists;
	}

	return NULL;
}

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
	u32 i = 0;
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	const struct __guc_mmio_reg_descr_group *reglists = guc->capture->reglists;
	const struct __guc_mmio_reg_descr_group *match;

	if (!reglists)
		return -ENODEV;

	match = guc_capture_get_one_list(reglists, owner, type, classid);
	if (match) {
		for (i = 0; i < num_entries && i < match->num_regs; ++i) {
			ptr[i].offset = match->list[i].reg.reg;
			ptr[i].value = 0xDEADF00D;
			ptr[i].flags = match->list[i].flags;
			ptr[i].mask = match->list[i].mask;
		}
		return 0;
	}

	guc_capture_warn_with_list_info(i915, "Missing register list init", owner, type,
					classid);

	return -ENODATA;
}

static int
guc_cap_list_num_regs(struct intel_guc_state_capture *gc, u32 owner, u32 type, u32 classid)
{
	const struct __guc_mmio_reg_descr_group *match;

	match = guc_capture_get_one_list(gc->reglists, owner, type, classid);
	if (!match)
		return 0;

	return match->num_regs;
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
}

int intel_guc_capture_init(struct intel_guc *guc)
{
	guc->capture = kzalloc(sizeof(*guc->capture), GFP_KERNEL);
	if (!guc->capture)
		return -ENOMEM;

	guc->capture->reglists = guc_capture_get_device_reglist(guc);

	return 0;
}

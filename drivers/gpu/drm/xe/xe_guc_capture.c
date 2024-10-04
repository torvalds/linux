// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2024 Intel Corporation
 */

#include <linux/types.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "abi/guc_actions_abi.h"
#include "abi/guc_capture_abi.h"
#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_guc_regs.h"
#include "regs/xe_regs.h"

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_exec_queue_types.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_guc_capture.h"
#include "xe_guc_capture_types.h"
#include "xe_guc_ct.h"
#include "xe_guc_log.h"
#include "xe_guc_submit.h"
#include "xe_hw_engine_types.h"
#include "xe_macros.h"
#include "xe_map.h"

/*
 * Define all device tables of GuC error capture register lists
 * NOTE:
 *     For engine-registers, GuC only needs the register offsets
 *     from the engine-mmio-base
 *
 *     64 bit registers need 2 entries for low 32 bit register and high 32 bit
 *     register, for example:
 *       Register           data_type       flags   mask    Register name
 *     { XXX_REG_LO(0),  REG_64BIT_LOW_DW,    0,      0,      NULL},
 *     { XXX_REG_HI(0),  REG_64BIT_HI_DW,,    0,      0,      "XXX_REG"},
 *     1. data_type: Indicate is hi/low 32 bit for a 64 bit register
 *                   A 64 bit register define requires 2 consecutive entries,
 *                   with low dword first and hi dword the second.
 *     2. Register name: null for incompleted define
 */
#define COMMON_XELP_BASE_GLOBAL \
	{ FORCEWAKE_GT,			REG_32BIT,	0,	0,	"FORCEWAKE_GT"}

#define COMMON_BASE_ENGINE_INSTANCE \
	{ RING_HWSTAM(0),		REG_32BIT,	0,	0,	"HWSTAM"}, \
	{ RING_HWS_PGA(0),		REG_32BIT,	0,	0,	"RING_HWS_PGA"}, \
	{ RING_HEAD(0),			REG_32BIT,	0,	0,	"RING_HEAD"}, \
	{ RING_TAIL(0),			REG_32BIT,	0,	0,	"RING_TAIL"}, \
	{ RING_CTL(0),			REG_32BIT,	0,	0,	"RING_CTL"}, \
	{ RING_MI_MODE(0),		REG_32BIT,	0,	0,	"RING_MI_MODE"}, \
	{ RING_MODE(0),			REG_32BIT,	0,	0,	"RING_MODE"}, \
	{ RING_ESR(0),			REG_32BIT,	0,	0,	"RING_ESR"}, \
	{ RING_EMR(0),			REG_32BIT,	0,	0,	"RING_EMR"}, \
	{ RING_EIR(0),			REG_32BIT,	0,	0,	"RING_EIR"}, \
	{ RING_IMR(0),			REG_32BIT,	0,	0,	"RING_IMR"}, \
	{ RING_IPEHR(0),		REG_32BIT,	0,	0,	"IPEHR"}, \
	{ RING_INSTDONE(0),		REG_32BIT,	0,	0,	"RING_INSTDONE"}, \
	{ INDIRECT_RING_STATE(0),	REG_32BIT,	0,	0,	"INDIRECT_RING_STATE"}, \
	{ RING_ACTHD(0),		REG_64BIT_LOW_DW, 0,	0,	NULL}, \
	{ RING_ACTHD_UDW(0),		REG_64BIT_HI_DW, 0,	0,	"ACTHD"}, \
	{ RING_BBADDR(0),		REG_64BIT_LOW_DW, 0,	0,	NULL}, \
	{ RING_BBADDR_UDW(0),		REG_64BIT_HI_DW, 0,	0,	"RING_BBADDR"}, \
	{ RING_START(0),		REG_64BIT_LOW_DW, 0,	0,	NULL}, \
	{ RING_START_UDW(0),		REG_64BIT_HI_DW, 0,	0,	"RING_START"}, \
	{ RING_DMA_FADD(0),		REG_64BIT_LOW_DW, 0,	0,	NULL}, \
	{ RING_DMA_FADD_UDW(0),		REG_64BIT_HI_DW, 0,	0,	"RING_DMA_FADD"}, \
	{ RING_EXECLIST_STATUS_LO(0),	REG_64BIT_LOW_DW, 0,	0,	NULL}, \
	{ RING_EXECLIST_STATUS_HI(0),	REG_64BIT_HI_DW, 0,	0,	"RING_EXECLIST_STATUS"}, \
	{ RING_EXECLIST_SQ_CONTENTS_LO(0), REG_64BIT_LOW_DW, 0,	0,	NULL}, \
	{ RING_EXECLIST_SQ_CONTENTS_HI(0), REG_64BIT_HI_DW, 0,	0,	"RING_EXECLIST_SQ_CONTENTS"}

#define COMMON_XELP_RC_CLASS \
	{ RCU_MODE,			REG_32BIT,	0,	0,	"RCU_MODE"}

#define COMMON_XELP_RC_CLASS_INSTDONE \
	{ SC_INSTDONE,			REG_32BIT,	0,	0,	"SC_INSTDONE"}, \
	{ SC_INSTDONE_EXTRA,		REG_32BIT,	0,	0,	"SC_INSTDONE_EXTRA"}, \
	{ SC_INSTDONE_EXTRA2,		REG_32BIT,	0,	0,	"SC_INSTDONE_EXTRA2"}

#define XELP_VEC_CLASS_REGS \
	{ SFC_DONE(0),			0,	0,	0,	"SFC_DONE[0]"}, \
	{ SFC_DONE(1),			0,	0,	0,	"SFC_DONE[1]"}, \
	{ SFC_DONE(2),			0,	0,	0,	"SFC_DONE[2]"}, \
	{ SFC_DONE(3),			0,	0,	0,	"SFC_DONE[3]"}

/* XE_LP Global */
static const struct __guc_mmio_reg_descr xe_lp_global_regs[] = {
	COMMON_XELP_BASE_GLOBAL,
};

/* Render / Compute Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_rc_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE,
};

/* Render / Compute Engine-Class */
static const struct __guc_mmio_reg_descr xe_rc_class_regs[] = {
	COMMON_XELP_RC_CLASS,
	COMMON_XELP_RC_CLASS_INSTDONE,
};

/* Render / Compute Engine-Class for xehpg */
static const struct __guc_mmio_reg_descr xe_hpg_rc_class_regs[] = {
	COMMON_XELP_RC_CLASS,
};

/* Media Decode/Encode Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_vd_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE,
};

/* Video Enhancement Engine-Class */
static const struct __guc_mmio_reg_descr xe_vec_class_regs[] = {
	XELP_VEC_CLASS_REGS,
};

/* Video Enhancement Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_vec_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE,
};

/* Blitter Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_blt_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE,
};

/* XE_LP - GSC Per-Engine-Instance */
static const struct __guc_mmio_reg_descr xe_lp_gsc_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE,
};

/*
 * Empty list to prevent warnings about unknown class/instance types
 * as not all class/instance types have entries on all platforms.
 */
static const struct __guc_mmio_reg_descr empty_regs_list[] = {
};

#define TO_GCAP_DEF_OWNER(x) (GUC_CAPTURE_LIST_INDEX_##x)
#define TO_GCAP_DEF_TYPE(x) (GUC_STATE_CAPTURE_TYPE_##x)
#define MAKE_REGLIST(regslist, regsowner, regstype, class) \
	{ \
		regslist, \
		ARRAY_SIZE(regslist), \
		TO_GCAP_DEF_OWNER(regsowner), \
		TO_GCAP_DEF_TYPE(regstype), \
		class \
	}

/* List of lists for legacy graphic product version < 1255 */
static const struct __guc_mmio_reg_descr_group xe_lp_lists[] = {
	MAKE_REGLIST(xe_lp_global_regs, PF, GLOBAL, 0),
	MAKE_REGLIST(xe_rc_class_regs, PF, ENGINE_CLASS, GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE),
	MAKE_REGLIST(xe_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_CAPTURE_LIST_CLASS_VIDEO),
	MAKE_REGLIST(xe_vd_inst_regs, PF, ENGINE_INSTANCE, GUC_CAPTURE_LIST_CLASS_VIDEO),
	MAKE_REGLIST(xe_vec_class_regs, PF, ENGINE_CLASS, GUC_CAPTURE_LIST_CLASS_VIDEOENHANCE),
	MAKE_REGLIST(xe_vec_inst_regs, PF, ENGINE_INSTANCE, GUC_CAPTURE_LIST_CLASS_VIDEOENHANCE),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_CAPTURE_LIST_CLASS_BLITTER),
	MAKE_REGLIST(xe_blt_inst_regs, PF, ENGINE_INSTANCE, GUC_CAPTURE_LIST_CLASS_BLITTER),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_CAPTURE_LIST_CLASS_GSC_OTHER),
	MAKE_REGLIST(xe_lp_gsc_inst_regs, PF, ENGINE_INSTANCE, GUC_CAPTURE_LIST_CLASS_GSC_OTHER),
	{}
};

 /* List of lists for graphic product version >= 1255 */
static const struct __guc_mmio_reg_descr_group xe_hpg_lists[] = {
	MAKE_REGLIST(xe_lp_global_regs, PF, GLOBAL, 0),
	MAKE_REGLIST(xe_hpg_rc_class_regs, PF, ENGINE_CLASS, GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE),
	MAKE_REGLIST(xe_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_CAPTURE_LIST_CLASS_VIDEO),
	MAKE_REGLIST(xe_vd_inst_regs, PF, ENGINE_INSTANCE, GUC_CAPTURE_LIST_CLASS_VIDEO),
	MAKE_REGLIST(xe_vec_class_regs, PF, ENGINE_CLASS, GUC_CAPTURE_LIST_CLASS_VIDEOENHANCE),
	MAKE_REGLIST(xe_vec_inst_regs, PF, ENGINE_INSTANCE, GUC_CAPTURE_LIST_CLASS_VIDEOENHANCE),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_CAPTURE_LIST_CLASS_BLITTER),
	MAKE_REGLIST(xe_blt_inst_regs, PF, ENGINE_INSTANCE, GUC_CAPTURE_LIST_CLASS_BLITTER),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_CAPTURE_LIST_CLASS_GSC_OTHER),
	MAKE_REGLIST(xe_lp_gsc_inst_regs, PF, ENGINE_INSTANCE, GUC_CAPTURE_LIST_CLASS_GSC_OTHER),
	{}
};

static const char * const capture_list_type_names[] = {
	"Global",
	"Class",
	"Instance",
};

static const char * const capture_engine_class_names[] = {
	"Render/Compute",
	"Video",
	"VideoEnhance",
	"Blitter",
	"GSC-Other",
};

struct __guc_capture_ads_cache {
	bool is_valid;
	void *ptr;
	size_t size;
	int status;
};

struct xe_guc_state_capture {
	const struct __guc_mmio_reg_descr_group *reglists;
	/**
	 * NOTE: steered registers have multiple instances depending on the HW configuration
	 * (slices or dual-sub-slices) and thus depends on HW fuses discovered
	 */
	struct __guc_mmio_reg_descr_group *extlists;
	struct __guc_capture_ads_cache ads_cache[GUC_CAPTURE_LIST_INDEX_MAX]
						[GUC_STATE_CAPTURE_TYPE_MAX]
						[GUC_CAPTURE_LIST_CLASS_MAX];
	void *ads_null_cache;
};

static const struct __guc_mmio_reg_descr_group *
guc_capture_get_device_reglist(struct xe_device *xe)
{
	if (GRAPHICS_VERx100(xe) >= 1255)
		return xe_hpg_lists;
	else
		return xe_lp_lists;
}

static const struct __guc_mmio_reg_descr_group *
guc_capture_get_one_list(const struct __guc_mmio_reg_descr_group *reglists,
			 u32 owner, u32 type, enum guc_capture_list_class_type capture_class)
{
	int i;

	if (!reglists)
		return NULL;

	for (i = 0; reglists[i].list; ++i) {
		if (reglists[i].owner == owner && reglists[i].type == type &&
		    (reglists[i].engine == capture_class ||
		     reglists[i].type == GUC_STATE_CAPTURE_TYPE_GLOBAL))
			return &reglists[i];
	}

	return NULL;
}

struct __ext_steer_reg {
	const char *name;
	struct xe_reg_mcr reg;
};

static const struct __ext_steer_reg xe_extregs[] = {
	{"SAMPLER_INSTDONE",		SAMPLER_INSTDONE},
	{"ROW_INSTDONE",		ROW_INSTDONE}
};

static const struct __ext_steer_reg xehpg_extregs[] = {
	{"SC_INSTDONE",			XEHPG_SC_INSTDONE},
	{"SC_INSTDONE_EXTRA",		XEHPG_SC_INSTDONE_EXTRA},
	{"SC_INSTDONE_EXTRA2",		XEHPG_SC_INSTDONE_EXTRA2},
	{"INSTDONE_GEOM_SVGUNIT",	XEHPG_INSTDONE_GEOM_SVGUNIT}
};

static void __fill_ext_reg(struct __guc_mmio_reg_descr *ext,
			   const struct __ext_steer_reg *extlist,
			   int slice_id, int subslice_id)
{
	if (!ext || !extlist)
		return;

	ext->reg = XE_REG(extlist->reg.__reg.addr);
	ext->flags = FIELD_PREP(GUC_REGSET_STEERING_NEEDED, 1);
	ext->flags = FIELD_PREP(GUC_REGSET_STEERING_GROUP, slice_id);
	ext->flags |= FIELD_PREP(GUC_REGSET_STEERING_INSTANCE, subslice_id);
	ext->regname = extlist->name;
}

static int
__alloc_ext_regs(struct drm_device *drm, struct __guc_mmio_reg_descr_group *newlist,
		 const struct __guc_mmio_reg_descr_group *rootlist, int num_regs)
{
	struct __guc_mmio_reg_descr *list;

	list = drmm_kzalloc(drm, num_regs * sizeof(struct __guc_mmio_reg_descr), GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	newlist->list = list;
	newlist->num_regs = num_regs;
	newlist->owner = rootlist->owner;
	newlist->engine = rootlist->engine;
	newlist->type = rootlist->type;

	return 0;
}

static int guc_capture_get_steer_reg_num(struct xe_device *xe)
{
	int num = ARRAY_SIZE(xe_extregs);

	if (GRAPHICS_VERx100(xe) >= 1255)
		num += ARRAY_SIZE(xehpg_extregs);

	return num;
}

static void guc_capture_alloc_steered_lists(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	u16 slice, subslice;
	int iter, i, total = 0;
	const struct __guc_mmio_reg_descr_group *lists = guc->capture->reglists;
	const struct __guc_mmio_reg_descr_group *list;
	struct __guc_mmio_reg_descr_group *extlists;
	struct __guc_mmio_reg_descr *extarray;
	bool has_xehpg_extregs = GRAPHICS_VERx100(gt_to_xe(gt)) >= 1255;
	struct drm_device *drm = &gt_to_xe(gt)->drm;
	bool has_rcs_ccs = false;
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	/*
	 * If GT has no rcs/ccs, no need to alloc steered list.
	 * Currently, only rcs/ccs has steering register, if in the future,
	 * other engine types has steering register, this condition check need
	 * to be extended
	 */
	for_each_hw_engine(hwe, gt, id) {
		if (xe_engine_class_to_guc_capture_class(hwe->class) ==
		    GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE) {
			has_rcs_ccs = true;
			break;
		}
	}

	if (!has_rcs_ccs)
		return;

	/* steered registers currently only exist for the render-class */
	list = guc_capture_get_one_list(lists, GUC_CAPTURE_LIST_INDEX_PF,
					GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS,
					GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE);
	/*
	 * Skip if this platform has no engine class registers or if extlists
	 * was previously allocated
	 */
	if (!list || guc->capture->extlists)
		return;

	total = bitmap_weight(gt->fuse_topo.g_dss_mask, sizeof(gt->fuse_topo.g_dss_mask) * 8) *
		guc_capture_get_steer_reg_num(guc_to_xe(guc));

	if (!total)
		return;

	/* allocate an extra for an end marker */
	extlists = drmm_kzalloc(drm, 2 * sizeof(struct __guc_mmio_reg_descr_group), GFP_KERNEL);
	if (!extlists)
		return;

	if (__alloc_ext_regs(drm, &extlists[0], list, total)) {
		drmm_kfree(drm, extlists);
		return;
	}

	/* For steering registers, the list is generated at run-time */
	extarray = (struct __guc_mmio_reg_descr *)extlists[0].list;
	for_each_dss_steering(iter, gt, slice, subslice) {
		for (i = 0; i < ARRAY_SIZE(xe_extregs); ++i) {
			__fill_ext_reg(extarray, &xe_extregs[i], slice, subslice);
			++extarray;
		}

		if (has_xehpg_extregs)
			for (i = 0; i < ARRAY_SIZE(xehpg_extregs); ++i) {
				__fill_ext_reg(extarray, &xehpg_extregs[i], slice, subslice);
				++extarray;
			}
	}

	extlists[0].num_regs = total;

	xe_gt_dbg(guc_to_gt(guc), "capture found %d ext-regs.\n", total);
	guc->capture->extlists = extlists;
}

static int
guc_capture_list_init(struct xe_guc *guc, u32 owner, u32 type,
		      enum guc_capture_list_class_type capture_class, struct guc_mmio_reg *ptr,
		      u16 num_entries)
{
	u32 ptr_idx = 0, list_idx = 0;
	const struct __guc_mmio_reg_descr_group *reglists = guc->capture->reglists;
	struct __guc_mmio_reg_descr_group *extlists = guc->capture->extlists;
	const struct __guc_mmio_reg_descr_group *match;
	u32 list_num;

	if (!reglists)
		return -ENODEV;

	match = guc_capture_get_one_list(reglists, owner, type, capture_class);
	if (!match)
		return -ENODATA;

	list_num = match->num_regs;
	for (list_idx = 0; ptr_idx < num_entries && list_idx < list_num; ++list_idx, ++ptr_idx) {
		ptr[ptr_idx].offset = match->list[list_idx].reg.addr;
		ptr[ptr_idx].value = 0xDEADF00D;
		ptr[ptr_idx].flags = match->list[list_idx].flags;
		ptr[ptr_idx].mask = match->list[list_idx].mask;
	}

	match = guc_capture_get_one_list(extlists, owner, type, capture_class);
	if (match)
		for (ptr_idx = list_num, list_idx = 0;
		     ptr_idx < num_entries && list_idx < match->num_regs;
		     ++ptr_idx, ++list_idx) {
			ptr[ptr_idx].offset = match->list[list_idx].reg.addr;
			ptr[ptr_idx].value = 0xDEADF00D;
			ptr[ptr_idx].flags = match->list[list_idx].flags;
			ptr[ptr_idx].mask = match->list[list_idx].mask;
		}

	if (ptr_idx < num_entries)
		xe_gt_dbg(guc_to_gt(guc), "Got short capture reglist init: %d out-of %d.\n",
			  ptr_idx, num_entries);

	return 0;
}

static int
guc_cap_list_num_regs(struct xe_guc *guc, u32 owner, u32 type,
		      enum guc_capture_list_class_type capture_class)
{
	const struct __guc_mmio_reg_descr_group *match;
	int num_regs = 0;

	match = guc_capture_get_one_list(guc->capture->reglists, owner, type, capture_class);
	if (match)
		num_regs = match->num_regs;

	match = guc_capture_get_one_list(guc->capture->extlists, owner, type, capture_class);
	if (match)
		num_regs += match->num_regs;
	else
		/* Estimate steering register size for rcs/ccs */
		if (capture_class == GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE)
			num_regs += guc_capture_get_steer_reg_num(guc_to_xe(guc)) *
				    XE_MAX_DSS_FUSE_BITS;

	return num_regs;
}

static int
guc_capture_getlistsize(struct xe_guc *guc, u32 owner, u32 type,
			enum guc_capture_list_class_type capture_class,
			size_t *size, bool is_purpose_est)
{
	struct xe_guc_state_capture *gc = guc->capture;
	struct xe_gt *gt = guc_to_gt(guc);
	struct __guc_capture_ads_cache *cache;
	int num_regs;

	xe_gt_assert(gt, type < GUC_STATE_CAPTURE_TYPE_MAX);
	xe_gt_assert(gt, capture_class < GUC_CAPTURE_LIST_CLASS_MAX);

	cache = &gc->ads_cache[owner][type][capture_class];
	if (!gc->reglists) {
		xe_gt_warn(gt, "No capture reglist for this device\n");
		return -ENODEV;
	}

	if (cache->is_valid) {
		*size = cache->size;
		return cache->status;
	}

	if (!is_purpose_est && owner == GUC_CAPTURE_LIST_INDEX_PF &&
	    !guc_capture_get_one_list(gc->reglists, owner, type, capture_class)) {
		if (type == GUC_STATE_CAPTURE_TYPE_GLOBAL)
			xe_gt_warn(gt, "Missing capture reglist: global!\n");
		else
			xe_gt_warn(gt, "Missing capture reglist: %s(%u):%s(%u)!\n",
				   capture_list_type_names[type], type,
				   capture_engine_class_names[capture_class], capture_class);
		return -ENODEV;
	}

	num_regs = guc_cap_list_num_regs(guc, owner, type, capture_class);
	/* intentional empty lists can exist depending on hw config */
	if (!num_regs)
		return -ENODATA;

	if (size)
		*size = PAGE_ALIGN((sizeof(struct guc_debug_capture_list)) +
				   (num_regs * sizeof(struct guc_mmio_reg)));

	return 0;
}

/**
 * xe_guc_capture_getlistsize - Get list size for owner/type/class combination
 * @guc: The GuC object
 * @owner: PF/VF owner
 * @type: GuC capture register type
 * @capture_class: GuC capture engine class id
 * @size: Point to the size
 *
 * This function will get the list for the owner/type/class combination, and
 * return the page aligned list size.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int
xe_guc_capture_getlistsize(struct xe_guc *guc, u32 owner, u32 type,
			   enum guc_capture_list_class_type capture_class, size_t *size)
{
	return guc_capture_getlistsize(guc, owner, type, capture_class, size, false);
}

/**
 * xe_guc_capture_getlist - Get register capture list for owner/type/class
 * combination
 * @guc:	The GuC object
 * @owner:	PF/VF owner
 * @type:	GuC capture register type
 * @capture_class:	GuC capture engine class id
 * @outptr:	Point to cached register capture list
 *
 * This function will get the register capture list for the owner/type/class
 * combination.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int
xe_guc_capture_getlist(struct xe_guc *guc, u32 owner, u32 type,
		       enum guc_capture_list_class_type capture_class, void **outptr)
{
	struct xe_guc_state_capture *gc = guc->capture;
	struct __guc_capture_ads_cache *cache = &gc->ads_cache[owner][type][capture_class];
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

	ret = xe_guc_capture_getlistsize(guc, owner, type, capture_class, &size);
	if (ret) {
		cache->is_valid = true;
		cache->ptr = NULL;
		cache->size = 0;
		cache->status = ret;
		return ret;
	}

	caplist = drmm_kzalloc(guc_to_drm(guc), size, GFP_KERNEL);
	if (!caplist)
		return -ENOMEM;

	/* populate capture list header */
	tmp = caplist;
	num_regs = guc_cap_list_num_regs(guc, owner, type, capture_class);
	listnode = (struct guc_debug_capture_list *)tmp;
	listnode->header.info = FIELD_PREP(GUC_CAPTURELISTHDR_NUMDESCR, (u32)num_regs);

	/* populate list of register descriptor */
	tmp += sizeof(struct guc_debug_capture_list);
	guc_capture_list_init(guc, owner, type, capture_class,
			      (struct guc_mmio_reg *)tmp, num_regs);

	/* cache this list */
	cache->is_valid = true;
	cache->ptr = caplist;
	cache->size = size;
	cache->status = 0;

	*outptr = caplist;

	return 0;
}

/**
 * xe_guc_capture_getnullheader - Get a null list for register capture
 * @guc:	The GuC object
 * @outptr:	Point to cached register capture list
 * @size:	Point to the size
 *
 * This function will alloc for a null list for register capture.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int
xe_guc_capture_getnullheader(struct xe_guc *guc, void **outptr, size_t *size)
{
	struct xe_guc_state_capture *gc = guc->capture;
	int tmp = sizeof(u32) * 4;
	void *null_header;

	if (gc->ads_null_cache) {
		*outptr = gc->ads_null_cache;
		*size = tmp;
		return 0;
	}

	null_header = drmm_kzalloc(guc_to_drm(guc), tmp, GFP_KERNEL);
	if (!null_header)
		return -ENOMEM;

	gc->ads_null_cache = null_header;
	*outptr = null_header;
	*size = tmp;

	return 0;
}

/**
 * xe_guc_capture_ads_input_worst_size - Calculate the worst size for GuC register capture
 * @guc: point to xe_guc structure
 *
 * Calculate the worst size for GuC register capture by including all possible engines classes.
 *
 * Returns: Calculated size
 */
size_t xe_guc_capture_ads_input_worst_size(struct xe_guc *guc)
{
	size_t total_size, class_size, instance_size, global_size;
	int i, j;

	/*
	 * This function calculates the worst case register lists size by
	 * including all possible engines classes. It is called during the
	 * first of a two-phase GuC (and ADS-population) initialization
	 * sequence, that is, during the pre-hwconfig phase before we have
	 * the exact engine fusing info.
	 */
	total_size = PAGE_SIZE;	/* Pad a page in front for empty lists */
	for (i = 0; i < GUC_CAPTURE_LIST_INDEX_MAX; i++) {
		for (j = 0; j < GUC_CAPTURE_LIST_CLASS_MAX; j++) {
			if (xe_guc_capture_getlistsize(guc, i,
						       GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS,
						       j, &class_size) < 0)
				class_size = 0;
			if (xe_guc_capture_getlistsize(guc, i,
						       GUC_STATE_CAPTURE_TYPE_ENGINE_INSTANCE,
						       j, &instance_size) < 0)
				instance_size = 0;
			total_size += class_size + instance_size;
		}
		if (xe_guc_capture_getlistsize(guc, i,
					       GUC_STATE_CAPTURE_TYPE_GLOBAL,
					       0, &global_size) < 0)
			global_size = 0;
		total_size += global_size;
	}

	return PAGE_ALIGN(total_size);
}

/*
 * xe_guc_capture_steered_list_init - Init steering register list
 * @guc: The GuC object
 *
 * Init steering register list for GuC register capture
 */
void xe_guc_capture_steered_list_init(struct xe_guc *guc)
{
	/*
	 * For certain engine classes, there are slice and subslice
	 * level registers requiring steering. We allocate and populate
	 * these based on hw config and add it as an extension list at
	 * the end of the pre-populated render list.
	 */
	guc_capture_alloc_steered_lists(guc);
}

/**
 * xe_guc_capture_init - Init for GuC register capture
 * @guc: The GuC object
 *
 * Init for GuC register capture, alloc memory for capture data structure.
 *
 * Returns: 0 if success.
 *	    -ENOMEM if out of memory
 */
int xe_guc_capture_init(struct xe_guc *guc)
{
	guc->capture = drmm_kzalloc(guc_to_drm(guc), sizeof(*guc->capture), GFP_KERNEL);
	if (!guc->capture)
		return -ENOMEM;

	guc->capture->reglists = guc_capture_get_device_reglist(guc_to_xe(guc));
	return 0;
}

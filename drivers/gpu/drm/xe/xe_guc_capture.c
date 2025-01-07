// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2024 Intel Corporation
 */

#include <linux/types.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "abi/guc_actions_abi.h"
#include "abi/guc_capture_abi.h"
#include "abi/guc_log_abi.h"
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
#include "xe_guc_ads.h"
#include "xe_guc_capture.h"
#include "xe_guc_capture_types.h"
#include "xe_guc_ct.h"
#include "xe_guc_exec_queue_types.h"
#include "xe_guc_log.h"
#include "xe_guc_submit_types.h"
#include "xe_guc_submit.h"
#include "xe_hw_engine_types.h"
#include "xe_hw_engine.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_sched_job.h"

/*
 * struct __guc_capture_bufstate
 *
 * Book-keeping structure used to track read and write pointers
 * as we extract error capture data from the GuC-log-buffer's
 * error-capture region as a stream of dwords.
 */
struct __guc_capture_bufstate {
	u32 size;
	u32 data_offset;
	u32 rd;
	u32 wr;
};

/*
 * struct __guc_capture_parsed_output - extracted error capture node
 *
 * A single unit of extracted error-capture output data grouped together
 * at an engine-instance level. We keep these nodes in a linked list.
 * See cachelist and outlist below.
 */
struct __guc_capture_parsed_output {
	/*
	 * A single set of 3 capture lists: a global-list
	 * an engine-class-list and an engine-instance list.
	 * outlist in __guc_capture_parsed_output will keep
	 * a linked list of these nodes that will eventually
	 * be detached from outlist and attached into to
	 * xe_codedump in response to a context reset
	 */
	struct list_head link;
	bool is_partial;
	u32 eng_class;
	u32 eng_inst;
	u32 guc_id;
	u32 lrca;
	u32 type;
	bool locked;
	enum xe_hw_engine_snapshot_source_id source;
	struct gcap_reg_list_info {
		u32 vfid;
		u32 num_regs;
		struct guc_mmio_reg *regs;
	} reginfo[GUC_STATE_CAPTURE_TYPE_MAX];
#define GCAP_PARSED_REGLIST_INDEX_GLOBAL   BIT(GUC_STATE_CAPTURE_TYPE_GLOBAL)
#define GCAP_PARSED_REGLIST_INDEX_ENGCLASS BIT(GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS)
};

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
 *     3. Incorrect order will trigger XE_WARN.
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
	struct list_head cachelist;
#define PREALLOC_NODES_MAX_COUNT (3 * GUC_MAX_ENGINE_CLASSES * GUC_MAX_INSTANCES_PER_CLASS)
#define PREALLOC_NODES_DEFAULT_NUMREGS 64

	int max_mmio_per_node;
	struct list_head outlist;
};

static void
guc_capture_remove_stale_matches_from_list(struct xe_guc_state_capture *gc,
					   struct __guc_capture_parsed_output *node);

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

const struct __guc_mmio_reg_descr_group *
xe_guc_capture_get_reg_desc_list(struct xe_gt *gt, u32 owner, u32 type,
				 enum guc_capture_list_class_type capture_class, bool is_ext)
{
	const struct __guc_mmio_reg_descr_group *reglists;

	if (is_ext) {
		struct xe_guc *guc = &gt->uc.guc;

		reglists = guc->capture->extlists;
	} else {
		reglists = guc_capture_get_device_reglist(gt_to_xe(gt));
	}
	return guc_capture_get_one_list(reglists, owner, type, capture_class);
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
		/*
		 * If a caller wants the full register dump size but we have
		 * not yet got the hw-config, which is before max_mmio_per_node
		 * is initialized, then provide a worst-case number for
		 * extlists based on max dss fuse bits, but only ever for
		 * render/compute
		 */
		if (owner == GUC_CAPTURE_LIST_INDEX_PF &&
		    type == GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS &&
		    capture_class == GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE &&
		    !guc->capture->max_mmio_per_node)
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

static int guc_capture_output_size_est(struct xe_guc *guc)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	int capture_size = 0;
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
	for_each_hw_engine(hwe, gt, id) {
		enum guc_capture_list_class_type capture_class;

		capture_class = xe_engine_class_to_guc_capture_class(hwe->class);
		capture_size += sizeof(struct guc_state_capture_group_header_t) +
					 (3 * sizeof(struct guc_state_capture_header_t));

		if (!guc_capture_getlistsize(guc, 0, GUC_STATE_CAPTURE_TYPE_GLOBAL,
					     0, &tmp, true))
			capture_size += tmp;
		if (!guc_capture_getlistsize(guc, 0, GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS,
					     capture_class, &tmp, true))
			capture_size += tmp;
		if (!guc_capture_getlistsize(guc, 0, GUC_STATE_CAPTURE_TYPE_ENGINE_INSTANCE,
					     capture_class, &tmp, true))
			capture_size += tmp;
	}

	return capture_size;
}

/*
 * Add on a 3x multiplier to allow for multiple back-to-back captures occurring
 * before the Xe can read the data out and process it
 */
#define GUC_CAPTURE_OVERBUFFER_MULTIPLIER 3

static void check_guc_capture_size(struct xe_guc *guc)
{
	int capture_size = guc_capture_output_size_est(guc);
	int spare_size = capture_size * GUC_CAPTURE_OVERBUFFER_MULTIPLIER;
	u32 buffer_size = xe_guc_log_section_size_capture(&guc->log);

	/*
	 * NOTE: capture_size is much smaller than the capture region
	 * allocation (DG2: <80K vs 1MB).
	 * Additionally, its based on space needed to fit all engines getting
	 * reset at once within the same G2H handler task slot. This is very
	 * unlikely. However, if GuC really does run out of space for whatever
	 * reason, we will see an separate warning message when processing the
	 * G2H event capture-notification, search for:
	 * xe_guc_STATE_CAPTURE_EVENT_STATUS_NOSPACE.
	 */
	if (capture_size < 0)
		xe_gt_dbg(guc_to_gt(guc),
			  "Failed to calculate error state capture buffer minimum size: %d!\n",
			  capture_size);
	if (capture_size > buffer_size)
		xe_gt_dbg(guc_to_gt(guc), "Error state capture buffer maybe small: %d < %d\n",
			  buffer_size, capture_size);
	else if (spare_size > buffer_size)
		xe_gt_dbg(guc_to_gt(guc),
			  "Error state capture buffer lacks spare size: %d < %d (min = %d)\n",
			  buffer_size, spare_size, capture_size);
}

static void
guc_capture_add_node_to_list(struct __guc_capture_parsed_output *node,
			     struct list_head *list)
{
	list_add(&node->link, list);
}

static void
guc_capture_add_node_to_outlist(struct xe_guc_state_capture *gc,
				struct __guc_capture_parsed_output *node)
{
	guc_capture_remove_stale_matches_from_list(gc, node);
	guc_capture_add_node_to_list(node, &gc->outlist);
}

static void
guc_capture_add_node_to_cachelist(struct xe_guc_state_capture *gc,
				  struct __guc_capture_parsed_output *node)
{
	guc_capture_add_node_to_list(node, &gc->cachelist);
}

static void
guc_capture_free_outlist_node(struct xe_guc_state_capture *gc,
			      struct __guc_capture_parsed_output *n)
{
	if (n) {
		n->locked = 0;
		list_del(&n->link);
		/* put node back to cache list */
		guc_capture_add_node_to_cachelist(gc, n);
	}
}

static void
guc_capture_remove_stale_matches_from_list(struct xe_guc_state_capture *gc,
					   struct __guc_capture_parsed_output *node)
{
	struct __guc_capture_parsed_output *n, *ntmp;
	int guc_id = node->guc_id;

	list_for_each_entry_safe(n, ntmp, &gc->outlist, link) {
		if (n != node && !n->locked && n->guc_id == guc_id)
			guc_capture_free_outlist_node(gc, n);
	}
}

static void
guc_capture_init_node(struct xe_guc *guc, struct __guc_capture_parsed_output *node)
{
	struct guc_mmio_reg *tmp[GUC_STATE_CAPTURE_TYPE_MAX];
	int i;

	for (i = 0; i < GUC_STATE_CAPTURE_TYPE_MAX; ++i) {
		tmp[i] = node->reginfo[i].regs;
		memset(tmp[i], 0, sizeof(struct guc_mmio_reg) *
		       guc->capture->max_mmio_per_node);
	}
	memset(node, 0, sizeof(*node));
	for (i = 0; i < GUC_STATE_CAPTURE_TYPE_MAX; ++i)
		node->reginfo[i].regs = tmp[i];

	INIT_LIST_HEAD(&node->link);
}

/**
 * DOC: Init, G2H-event and reporting flows for GuC-error-capture
 *
 * KMD Init time flows:
 * --------------------
 *     --> alloc A: GuC input capture regs lists (registered to GuC via ADS).
 *                  xe_guc_ads acquires the register lists by calling
 *                  xe_guc_capture_getlistsize and xe_guc_capture_getlist 'n' times,
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
 *                   L--> xe_guc_capture_process
 *                           L--> Loop through B (head..tail) and for each engine instance's
 *                                err-state-captured register-list we find, we alloc 'C':
 *      --> alloc C: A capture-output-node structure that includes misc capture info along
 *                   with 3 register list dumps (global, engine-class and engine-instance)
 *                   This node is created from a pre-allocated list of blank nodes in
 *                   guc->capture->cachelist and populated with the error-capture
 *                   data from GuC and then it's added into guc->capture->outlist linked
 *                   list. This list is used for matchup and printout by xe_devcoredump_read
 *                   and xe_engine_snapshot_print, (when user invokes the devcoredump sysfs).
 *
 * GUC --> notify context reset:
 * -----------------------------
 *     --> guc_exec_queue_timedout_job
 *                   L--> xe_devcoredump
 *                          L--> devcoredump_snapshot
 *                               --> xe_hw_engine_snapshot_capture
 *                               --> xe_engine_manual_capture(For manual capture)
 *
 * User Sysfs / Debugfs
 * --------------------
 *      --> xe_devcoredump_read->
 *             L--> xxx_snapshot_print
 *                    L--> xe_engine_snapshot_print
 *                         Print register lists values saved at
 *                         guc->capture->outlist
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
 * Below function, guc_capture_log_remove_bytes is a helper for that. All callers of this
 * function would typically do a straight-up memcpy from the ring contents and will only
 * call this helper if their structure-extraction is straddling across the end of the
 * ring. GuC firmware does not add any padding. The reason for the no-padding is to ease
 * scalability for future expansion of output data types without requiring a redesign
 * of the flow controls.
 */
static int
guc_capture_log_remove_bytes(struct xe_guc *guc, struct __guc_capture_bufstate *buf,
			     void *out, int bytes_needed)
{
#define GUC_CAPTURE_LOG_BUF_COPY_RETRY_MAX	3

	int fill_size = 0, tries = GUC_CAPTURE_LOG_BUF_COPY_RETRY_MAX;
	int copy_size, avail;

	xe_assert(guc_to_xe(guc), bytes_needed % sizeof(u32) == 0);

	if (bytes_needed > guc_capture_buf_cnt(buf))
		return -1;

	while (bytes_needed > 0 && tries--) {
		int misaligned;

		avail = guc_capture_buf_cnt_to_end(buf);
		misaligned = avail % sizeof(u32);
		/* wrap if at end */
		if (!avail) {
			/* output stream clipped */
			if (!buf->rd)
				return fill_size;
			buf->rd = 0;
			continue;
		}

		/* Only copy to u32 aligned data */
		copy_size = avail < bytes_needed ? avail - misaligned : bytes_needed;
		xe_map_memcpy_from(guc_to_xe(guc), out + fill_size, &guc->log.bo->vmap,
				   buf->data_offset + buf->rd, copy_size);
		buf->rd += copy_size;
		fill_size += copy_size;
		bytes_needed -= copy_size;

		if (misaligned)
			xe_gt_warn(guc_to_gt(guc),
				   "Bytes extraction not dword aligned, clipping.\n");
	}

	return fill_size;
}

static int
guc_capture_log_get_group_hdr(struct xe_guc *guc, struct __guc_capture_bufstate *buf,
			      struct guc_state_capture_group_header_t *ghdr)
{
	int fullsize = sizeof(struct guc_state_capture_group_header_t);

	if (guc_capture_log_remove_bytes(guc, buf, ghdr, fullsize) != fullsize)
		return -1;
	return 0;
}

static int
guc_capture_log_get_data_hdr(struct xe_guc *guc, struct __guc_capture_bufstate *buf,
			     struct guc_state_capture_header_t *hdr)
{
	int fullsize = sizeof(struct guc_state_capture_header_t);

	if (guc_capture_log_remove_bytes(guc, buf, hdr, fullsize) != fullsize)
		return -1;
	return 0;
}

static int
guc_capture_log_get_register(struct xe_guc *guc, struct __guc_capture_bufstate *buf,
			     struct guc_mmio_reg *reg)
{
	int fullsize = sizeof(struct guc_mmio_reg);

	if (guc_capture_log_remove_bytes(guc, buf, reg, fullsize) != fullsize)
		return -1;
	return 0;
}

static struct __guc_capture_parsed_output *
guc_capture_get_prealloc_node(struct xe_guc *guc)
{
	struct __guc_capture_parsed_output *found = NULL;

	if (!list_empty(&guc->capture->cachelist)) {
		struct __guc_capture_parsed_output *n, *ntmp;

		/* get first avail node from the cache list */
		list_for_each_entry_safe(n, ntmp, &guc->capture->cachelist, link) {
			found = n;
			break;
		}
	} else {
		struct __guc_capture_parsed_output *n, *ntmp;

		/*
		 * traverse reversed and steal back the oldest node already
		 * allocated
		 */
		list_for_each_entry_safe_reverse(n, ntmp, &guc->capture->outlist, link) {
			if (!n->locked)
				found = n;
		}
	}
	if (found) {
		list_del(&found->link);
		guc_capture_init_node(guc, found);
	}

	return found;
}

static struct __guc_capture_parsed_output *
guc_capture_clone_node(struct xe_guc *guc, struct __guc_capture_parsed_output *original,
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
	for (i = 0; i < GUC_STATE_CAPTURE_TYPE_MAX; ++i) {
		if (keep_reglist_mask & BIT(i)) {
			XE_WARN_ON(original->reginfo[i].num_regs  >
				   guc->capture->max_mmio_per_node);

			memcpy(new->reginfo[i].regs, original->reginfo[i].regs,
			       original->reginfo[i].num_regs * sizeof(struct guc_mmio_reg));

			new->reginfo[i].num_regs = original->reginfo[i].num_regs;
			new->reginfo[i].vfid  = original->reginfo[i].vfid;

			if (i == GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS) {
				new->eng_class = original->eng_class;
			} else if (i == GUC_STATE_CAPTURE_TYPE_ENGINE_INSTANCE) {
				new->eng_inst = original->eng_inst;
				new->guc_id = original->guc_id;
				new->lrca = original->lrca;
			}
		}
	}

	return new;
}

static int
guc_capture_extract_reglists(struct xe_guc *guc, struct __guc_capture_bufstate *buf)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct guc_state_capture_group_header_t ghdr = {0};
	struct guc_state_capture_header_t hdr = {0};
	struct __guc_capture_parsed_output *node = NULL;
	struct guc_mmio_reg *regs = NULL;
	int i, numlists, numregs, ret = 0;
	enum guc_state_capture_type datatype;
	struct guc_mmio_reg tmp;
	bool is_partial = false;

	i = guc_capture_buf_cnt(buf);
	if (!i)
		return -ENODATA;

	if (i % sizeof(u32)) {
		xe_gt_warn(gt, "Got mis-aligned register capture entries\n");
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
	is_partial = FIELD_GET(GUC_STATE_CAPTURE_GROUP_HEADER_CAPTURE_GROUP_TYPE, ghdr.info);
	numlists = FIELD_GET(GUC_STATE_CAPTURE_GROUP_HEADER_NUM_CAPTURES, ghdr.info);

	while (numlists--) {
		if (guc_capture_log_get_data_hdr(guc, buf, &hdr)) {
			ret = -EIO;
			break;
		}

		datatype = FIELD_GET(GUC_STATE_CAPTURE_HEADER_CAPTURE_TYPE, hdr.info);
		if (datatype > GUC_STATE_CAPTURE_TYPE_ENGINE_INSTANCE) {
			/* unknown capture type - skip over to next capture set */
			numregs = FIELD_GET(GUC_STATE_CAPTURE_HEADER_NUM_MMIO_ENTRIES,
					    hdr.num_mmio_entries);
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
			 * linked list for match-up when xe_devcoredump calls later
			 * (and alloc a blank node for the next set of reglists)
			 * or continue with the same node or clone the current node
			 * but only retain the global or class registers (such as the
			 * case of dependent engine resets).
			 */
			if (datatype == GUC_STATE_CAPTURE_TYPE_GLOBAL) {
				guc_capture_add_node_to_outlist(guc->capture, node);
				node = NULL;
			} else if (datatype == GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS &&
				   node->reginfo[GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS].num_regs) {
				/* Add to list, clone node and duplicate global list */
				guc_capture_add_node_to_outlist(guc->capture, node);
				node = guc_capture_clone_node(guc, node,
							      GCAP_PARSED_REGLIST_INDEX_GLOBAL);
			} else if (datatype == GUC_STATE_CAPTURE_TYPE_ENGINE_INSTANCE &&
				   node->reginfo[GUC_STATE_CAPTURE_TYPE_ENGINE_INSTANCE].num_regs) {
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
			if (datatype != GUC_STATE_CAPTURE_TYPE_GLOBAL)
				xe_gt_dbg(gt, "Register capture missing global dump: %08x!\n",
					  datatype);
		}
		node->is_partial = is_partial;
		node->reginfo[datatype].vfid = FIELD_GET(GUC_STATE_CAPTURE_HEADER_VFID, hdr.owner);
		node->source = XE_ENGINE_CAPTURE_SOURCE_GUC;
		node->type = datatype;

		switch (datatype) {
		case GUC_STATE_CAPTURE_TYPE_ENGINE_INSTANCE:
			node->eng_class = FIELD_GET(GUC_STATE_CAPTURE_HEADER_ENGINE_CLASS,
						    hdr.info);
			node->eng_inst = FIELD_GET(GUC_STATE_CAPTURE_HEADER_ENGINE_INSTANCE,
						   hdr.info);
			node->lrca = hdr.lrca;
			node->guc_id = hdr.guc_id;
			break;
		case GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS:
			node->eng_class = FIELD_GET(GUC_STATE_CAPTURE_HEADER_ENGINE_CLASS,
						    hdr.info);
			break;
		default:
			break;
		}

		numregs = FIELD_GET(GUC_STATE_CAPTURE_HEADER_NUM_MMIO_ENTRIES,
				    hdr.num_mmio_entries);
		if (numregs > guc->capture->max_mmio_per_node) {
			xe_gt_dbg(gt, "Register capture list extraction clipped by prealloc!\n");
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
		/* If we have data, add to linked list for match-up when xe_devcoredump calls */
		for (i = GUC_STATE_CAPTURE_TYPE_GLOBAL; i < GUC_STATE_CAPTURE_TYPE_MAX; ++i) {
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

static int __guc_capture_flushlog_complete(struct xe_guc *guc)
{
	u32 action[] = {
		XE_GUC_ACTION_LOG_BUFFER_FILE_FLUSH_COMPLETE,
		GUC_LOG_BUFFER_CAPTURE
	};

	return xe_guc_ct_send_g2h_handler(&guc->ct, action, ARRAY_SIZE(action));
}

static void __guc_capture_process_output(struct xe_guc *guc)
{
	unsigned int buffer_size, read_offset, write_offset, full_count;
	struct xe_uc *uc = container_of(guc, typeof(*uc), guc);
	struct guc_log_buffer_state log_buf_state_local;
	struct __guc_capture_bufstate buf;
	bool new_overflow;
	int ret, tmp;
	u32 log_buf_state_offset;
	u32 src_data_offset;

	log_buf_state_offset = sizeof(struct guc_log_buffer_state) * GUC_LOG_BUFFER_CAPTURE;
	src_data_offset = xe_guc_get_log_buffer_offset(&guc->log, GUC_LOG_BUFFER_CAPTURE);

	/*
	 * Make a copy of the state structure, inside GuC log buffer
	 * (which is uncached mapped), on the stack to avoid reading
	 * from it multiple times.
	 */
	xe_map_memcpy_from(guc_to_xe(guc), &log_buf_state_local, &guc->log.bo->vmap,
			   log_buf_state_offset, sizeof(struct guc_log_buffer_state));

	buffer_size = xe_guc_get_log_buffer_size(&guc->log, GUC_LOG_BUFFER_CAPTURE);
	read_offset = log_buf_state_local.read_ptr;
	write_offset = log_buf_state_local.sampled_write_ptr;
	full_count = FIELD_GET(GUC_LOG_BUFFER_STATE_BUFFER_FULL_CNT, log_buf_state_local.flags);

	/* Bookkeeping stuff */
	tmp = FIELD_GET(GUC_LOG_BUFFER_STATE_FLUSH_TO_FILE, log_buf_state_local.flags);
	guc->log.stats[GUC_LOG_BUFFER_CAPTURE].flush += tmp;
	new_overflow = xe_guc_check_log_buf_overflow(&guc->log, GUC_LOG_BUFFER_CAPTURE,
						     full_count);

	/* Now copy the actual logs. */
	if (unlikely(new_overflow)) {
		/* copy the whole buffer in case of overflow */
		read_offset = 0;
		write_offset = buffer_size;
	} else if (unlikely((read_offset > buffer_size) ||
			(write_offset > buffer_size))) {
		xe_gt_err(guc_to_gt(guc),
			  "Register capture buffer in invalid state: read = 0x%X, size = 0x%X!\n",
			  read_offset, buffer_size);
		/* copy whole buffer as offsets are unreliable */
		read_offset = 0;
		write_offset = buffer_size;
	}

	buf.size = buffer_size;
	buf.rd = read_offset;
	buf.wr = write_offset;
	buf.data_offset = src_data_offset;

	if (!xe_guc_read_stopped(guc)) {
		do {
			ret = guc_capture_extract_reglists(guc, &buf);
			if (ret && ret != -ENODATA)
				xe_gt_dbg(guc_to_gt(guc), "Capture extraction failed:%d\n", ret);
		} while (ret >= 0);
	}

	/* Update the state of log buffer err-cap state */
	xe_map_wr(guc_to_xe(guc), &guc->log.bo->vmap,
		  log_buf_state_offset + offsetof(struct guc_log_buffer_state, read_ptr), u32,
		  write_offset);

	/*
	 * Clear the flush_to_file from local first, the local was loaded by above
	 * xe_map_memcpy_from, then write out the "updated local" through
	 * xe_map_wr()
	 */
	log_buf_state_local.flags &= ~GUC_LOG_BUFFER_STATE_FLUSH_TO_FILE;
	xe_map_wr(guc_to_xe(guc), &guc->log.bo->vmap,
		  log_buf_state_offset + offsetof(struct guc_log_buffer_state, flags), u32,
		  log_buf_state_local.flags);
	__guc_capture_flushlog_complete(guc);
}

/*
 * xe_guc_capture_process - Process GuC register captured data
 * @guc: The GuC object
 *
 * When GuC captured data is ready, GuC will send message
 * XE_GUC_ACTION_STATE_CAPTURE_NOTIFICATION to host, this function will be
 * called to process the data comes with the message.
 *
 * Returns: None
 */
void xe_guc_capture_process(struct xe_guc *guc)
{
	if (guc->capture)
		__guc_capture_process_output(guc);
}

static struct __guc_capture_parsed_output *
guc_capture_alloc_one_node(struct xe_guc *guc)
{
	struct drm_device *drm = guc_to_drm(guc);
	struct __guc_capture_parsed_output *new;
	int i;

	new = drmm_kzalloc(drm, sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	for (i = 0; i < GUC_STATE_CAPTURE_TYPE_MAX; ++i) {
		new->reginfo[i].regs = drmm_kzalloc(drm, guc->capture->max_mmio_per_node *
						    sizeof(struct guc_mmio_reg), GFP_KERNEL);
		if (!new->reginfo[i].regs) {
			while (i)
				drmm_kfree(drm, new->reginfo[--i].regs);
			drmm_kfree(drm, new);
			return NULL;
		}
	}
	guc_capture_init_node(guc, new);

	return new;
}

static void
__guc_capture_create_prealloc_nodes(struct xe_guc *guc)
{
	struct __guc_capture_parsed_output *node = NULL;
	int i;

	for (i = 0; i < PREALLOC_NODES_MAX_COUNT; ++i) {
		node = guc_capture_alloc_one_node(guc);
		if (!node) {
			xe_gt_warn(guc_to_gt(guc), "Register capture pre-alloc-cache failure\n");
			/* dont free the priors, use what we got and cleanup at shutdown */
			return;
		}
		guc_capture_add_node_to_cachelist(guc->capture, node);
	}
}

static int
guc_get_max_reglist_count(struct xe_guc *guc)
{
	int i, j, k, tmp, maxregcount = 0;

	for (i = 0; i < GUC_CAPTURE_LIST_INDEX_MAX; ++i) {
		for (j = 0; j < GUC_STATE_CAPTURE_TYPE_MAX; ++j) {
			for (k = 0; k < GUC_CAPTURE_LIST_CLASS_MAX; ++k) {
				const struct __guc_mmio_reg_descr_group *match;

				if (j == GUC_STATE_CAPTURE_TYPE_GLOBAL && k > 0)
					continue;

				tmp = 0;
				match = guc_capture_get_one_list(guc->capture->reglists, i, j, k);
				if (match)
					tmp = match->num_regs;

				match = guc_capture_get_one_list(guc->capture->extlists, i, j, k);
				if (match)
					tmp += match->num_regs;

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
guc_capture_create_prealloc_nodes(struct xe_guc *guc)
{
	/* skip if we've already done the pre-alloc */
	if (guc->capture->max_mmio_per_node)
		return;

	guc->capture->max_mmio_per_node = guc_get_max_reglist_count(guc);
	__guc_capture_create_prealloc_nodes(guc);
}

static void
read_reg_to_node(struct xe_hw_engine *hwe, const struct __guc_mmio_reg_descr_group *list,
		 struct guc_mmio_reg *regs)
{
	int i;

	if (!list || !list->list || list->num_regs == 0)
		return;

	if (!regs)
		return;

	for (i = 0; i < list->num_regs; i++) {
		struct __guc_mmio_reg_descr desc = list->list[i];
		u32 value;

		if (list->type == GUC_STATE_CAPTURE_TYPE_ENGINE_INSTANCE) {
			value = xe_hw_engine_mmio_read32(hwe, desc.reg);
		} else {
			if (list->type == GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS &&
			    FIELD_GET(GUC_REGSET_STEERING_NEEDED, desc.flags)) {
				int group, instance;

				group = FIELD_GET(GUC_REGSET_STEERING_GROUP, desc.flags);
				instance = FIELD_GET(GUC_REGSET_STEERING_INSTANCE, desc.flags);
				value = xe_gt_mcr_unicast_read(hwe->gt, XE_REG_MCR(desc.reg.addr),
							       group, instance);
			} else {
				value = xe_mmio_read32(&hwe->gt->mmio, desc.reg);
			}
		}

		regs[i].value = value;
		regs[i].offset = desc.reg.addr;
		regs[i].flags = desc.flags;
		regs[i].mask = desc.mask;
	}
}

/**
 * xe_engine_manual_capture - Take a manual engine snapshot from engine.
 * @hwe: Xe HW Engine.
 * @snapshot: The engine snapshot
 *
 * Take engine snapshot from engine read.
 *
 * Returns: None
 */
void
xe_engine_manual_capture(struct xe_hw_engine *hwe, struct xe_hw_engine_snapshot *snapshot)
{
	struct xe_gt *gt = hwe->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_guc *guc = &gt->uc.guc;
	struct xe_devcoredump *devcoredump = &xe->devcoredump;
	enum guc_capture_list_class_type capture_class;
	const struct __guc_mmio_reg_descr_group *list;
	struct __guc_capture_parsed_output *new;
	enum guc_state_capture_type type;
	u16 guc_id = 0;
	u32 lrca = 0;

	if (IS_SRIOV_VF(xe))
		return;

	new = guc_capture_get_prealloc_node(guc);
	if (!new)
		return;

	capture_class = xe_engine_class_to_guc_capture_class(hwe->class);
	for (type = GUC_STATE_CAPTURE_TYPE_GLOBAL; type < GUC_STATE_CAPTURE_TYPE_MAX; type++) {
		struct gcap_reg_list_info *reginfo = &new->reginfo[type];
		/*
		 * regsinfo->regs is allocated based on guc->capture->max_mmio_per_node
		 * which is based on the descriptor list driving the population so
		 * should not overflow
		 */

		/* Get register list for the type/class */
		list = xe_guc_capture_get_reg_desc_list(gt, GUC_CAPTURE_LIST_INDEX_PF, type,
							capture_class, false);
		if (!list) {
			xe_gt_dbg(gt, "Empty GuC capture register descriptor for %s",
				  hwe->name);
			continue;
		}

		read_reg_to_node(hwe, list, reginfo->regs);
		reginfo->num_regs = list->num_regs;

		/* Capture steering registers for rcs/ccs */
		if (capture_class == GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE) {
			list = xe_guc_capture_get_reg_desc_list(gt, GUC_CAPTURE_LIST_INDEX_PF,
								type, capture_class, true);
			if (list) {
				read_reg_to_node(hwe, list, &reginfo->regs[reginfo->num_regs]);
				reginfo->num_regs += list->num_regs;
			}
		}
	}

	if (devcoredump && devcoredump->captured) {
		struct xe_guc_submit_exec_queue_snapshot *ge = devcoredump->snapshot.ge;

		if (ge) {
			guc_id = ge->guc.id;
			if (ge->lrc[0])
				lrca = ge->lrc[0]->context_desc;
		}
	}

	new->eng_class = xe_engine_class_to_guc_class(hwe->class);
	new->eng_inst = hwe->instance;
	new->guc_id = guc_id;
	new->lrca = lrca;
	new->is_partial = 0;
	new->locked = 1;
	new->source = XE_ENGINE_CAPTURE_SOURCE_MANUAL;

	guc_capture_add_node_to_outlist(guc->capture, new);
	devcoredump->snapshot.matched_node = new;
}

static struct guc_mmio_reg *
guc_capture_find_reg(struct gcap_reg_list_info *reginfo, u32 addr, u32 flags)
{
	int i;

	if (reginfo && reginfo->num_regs > 0) {
		struct guc_mmio_reg *regs = reginfo->regs;

		if (regs)
			for (i = 0; i < reginfo->num_regs; i++)
				if (regs[i].offset == addr && regs[i].flags == flags)
					return &regs[i];
	}

	return NULL;
}

static void
snapshot_print_by_list_order(struct xe_hw_engine_snapshot *snapshot, struct drm_printer *p,
			     u32 type, const struct __guc_mmio_reg_descr_group *list)
{
	struct xe_gt *gt = snapshot->hwe->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_guc *guc = &gt->uc.guc;
	struct xe_devcoredump *devcoredump = &xe->devcoredump;
	struct xe_devcoredump_snapshot *devcore_snapshot = &devcoredump->snapshot;
	struct gcap_reg_list_info *reginfo = NULL;
	u32 i, last_value = 0;
	bool is_ext, low32_ready = false;

	if (!list || !list->list || list->num_regs == 0)
		return;
	XE_WARN_ON(!devcore_snapshot->matched_node);

	is_ext = list == guc->capture->extlists;
	reginfo = &devcore_snapshot->matched_node->reginfo[type];

	/*
	 * loop through descriptor first and find the register in the node
	 * this is more scalable for developer maintenance as it will ensure
	 * the printout matched the ordering of the static descriptor
	 * table-of-lists
	 */
	for (i = 0; i < list->num_regs; i++) {
		const struct __guc_mmio_reg_descr *reg_desc = &list->list[i];
		struct guc_mmio_reg *reg;
		u32 value;

		reg = guc_capture_find_reg(reginfo, reg_desc->reg.addr, reg_desc->flags);
		if (!reg)
			continue;

		value = reg->value;
		switch (reg_desc->data_type) {
		case REG_64BIT_LOW_DW:
			last_value = value;

			/*
			 * A 64 bit register define requires 2 consecutive
			 * entries in register list, with low dword first
			 * and hi dword the second, like:
			 *  { XXX_REG_LO(0), REG_64BIT_LOW_DW, 0, 0, NULL},
			 *  { XXX_REG_HI(0), REG_64BIT_HI_DW,  0, 0, "XXX_REG"},
			 *
			 * Incorrect order will trigger XE_WARN.
			 *
			 * Possible double low here, for example:
			 *  { XXX_REG_LO(0), REG_64BIT_LOW_DW, 0, 0, NULL},
			 *  { XXX_REG_LO(0), REG_64BIT_LOW_DW, 0, 0, NULL},
			 */
			XE_WARN_ON(low32_ready);
			low32_ready = true;
			/* Low 32 bit dword saved, continue for high 32 bit */
			break;

		case REG_64BIT_HI_DW: {
			u64 value_qw = ((u64)value << 32) | last_value;

			/*
			 * Incorrect 64bit register order. Possible missing low.
			 * for example:
			 *  { XXX_REG(0), REG_32BIT, 0, 0, NULL},
			 *  { XXX_REG_HI(0), REG_64BIT_HI_DW, 0, 0, NULL},
			 */
			XE_WARN_ON(!low32_ready);
			low32_ready = false;

			drm_printf(p, "\t%s: 0x%016llx\n", reg_desc->regname, value_qw);
			break;
		}

		case REG_32BIT:
			/*
			 * Incorrect 64bit register order. Possible missing high.
			 * for example:
			 *  { XXX_REG_LO(0), REG_64BIT_LOW_DW, 0, 0, NULL},
			 *  { XXX_REG(0), REG_32BIT, 0, 0, "XXX_REG"},
			 */
			XE_WARN_ON(low32_ready);

			if (is_ext) {
				int dss, group, instance;

				group = FIELD_GET(GUC_REGSET_STEERING_GROUP, reg_desc->flags);
				instance = FIELD_GET(GUC_REGSET_STEERING_INSTANCE, reg_desc->flags);
				dss = xe_gt_mcr_steering_info_to_dss_id(gt, group, instance);

				drm_printf(p, "\t%s[%u]: 0x%08x\n", reg_desc->regname, dss, value);
			} else {
				drm_printf(p, "\t%s: 0x%08x\n", reg_desc->regname, value);
			}
			break;
		}
	}

	/*
	 * Incorrect 64bit register order. Possible missing high.
	 * for example:
	 *  { XXX_REG_LO(0), REG_64BIT_LOW_DW, 0, 0, NULL},
	 *  } // <- Register list end
	 */
	XE_WARN_ON(low32_ready);
}

/**
 * xe_engine_snapshot_print - Print out a given Xe HW Engine snapshot.
 * @snapshot: Xe HW Engine snapshot object.
 * @p: drm_printer where it will be printed out.
 *
 * This function prints out a given Xe HW Engine snapshot object.
 */
void xe_engine_snapshot_print(struct xe_hw_engine_snapshot *snapshot, struct drm_printer *p)
{
	const char *grptype[GUC_STATE_CAPTURE_GROUP_TYPE_MAX] = {
		"full-capture",
		"partial-capture"
	};
	int type;
	const struct __guc_mmio_reg_descr_group *list;
	enum guc_capture_list_class_type capture_class;

	struct xe_gt *gt;
	struct xe_device *xe;
	struct xe_devcoredump *devcoredump;
	struct xe_devcoredump_snapshot *devcore_snapshot;

	if (!snapshot)
		return;

	gt = snapshot->hwe->gt;
	xe = gt_to_xe(gt);
	devcoredump = &xe->devcoredump;
	devcore_snapshot = &devcoredump->snapshot;

	if (!devcore_snapshot->matched_node)
		return;

	xe_gt_assert(gt, snapshot->hwe);

	capture_class = xe_engine_class_to_guc_capture_class(snapshot->hwe->class);

	drm_printf(p, "%s (physical), logical instance=%d\n",
		   snapshot->name ? snapshot->name : "",
		   snapshot->logical_instance);
	drm_printf(p, "\tCapture_source: %s\n",
		   devcore_snapshot->matched_node->source == XE_ENGINE_CAPTURE_SOURCE_GUC ?
		   "GuC" : "Manual");
	drm_printf(p, "\tCoverage: %s\n", grptype[devcore_snapshot->matched_node->is_partial]);
	drm_printf(p, "\tForcewake: domain 0x%x, ref %d\n",
		   snapshot->forcewake.domain, snapshot->forcewake.ref);
	drm_printf(p, "\tReserved: %s\n",
		   str_yes_no(snapshot->kernel_reserved));

	for (type = GUC_STATE_CAPTURE_TYPE_GLOBAL; type < GUC_STATE_CAPTURE_TYPE_MAX; type++) {
		list = xe_guc_capture_get_reg_desc_list(gt, GUC_CAPTURE_LIST_INDEX_PF, type,
							capture_class, false);
		snapshot_print_by_list_order(snapshot, p, type, list);
	}

	if (capture_class == GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE) {
		list = xe_guc_capture_get_reg_desc_list(gt, GUC_CAPTURE_LIST_INDEX_PF,
							GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS,
							capture_class, true);
		snapshot_print_by_list_order(snapshot, p, GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS,
					     list);
	}

	drm_puts(p, "\n");
}

/**
 * xe_guc_capture_get_matching_and_lock - Matching GuC capture for the queue.
 * @q: The exec queue object
 *
 * Search within the capture outlist for the queue, could be used for check if
 * GuC capture is ready for the queue.
 * If found, the locked boolean of the node will be flagged.
 *
 * Returns: found guc-capture node ptr else NULL
 */
struct __guc_capture_parsed_output *
xe_guc_capture_get_matching_and_lock(struct xe_exec_queue *q)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct xe_device *xe;
	u16 guc_class = GUC_LAST_ENGINE_CLASS + 1;
	struct xe_devcoredump_snapshot *ss;

	if (!q || !q->gt)
		return NULL;

	xe = gt_to_xe(q->gt);
	if (xe->wedged.mode >= 2 || !xe_device_uc_enabled(xe) || IS_SRIOV_VF(xe))
		return NULL;

	ss = &xe->devcoredump.snapshot;
	if (ss->matched_node && ss->matched_node->source == XE_ENGINE_CAPTURE_SOURCE_GUC)
		return ss->matched_node;

	/* Find hwe for the queue */
	for_each_hw_engine(hwe, q->gt, id) {
		if (hwe != q->hwe)
			continue;
		guc_class = xe_engine_class_to_guc_class(hwe->class);
		break;
	}

	if (guc_class <= GUC_LAST_ENGINE_CLASS) {
		struct __guc_capture_parsed_output *n, *ntmp;
		struct xe_guc *guc =  &q->gt->uc.guc;
		u16 guc_id = q->guc->id;
		u32 lrca = xe_lrc_ggtt_addr(q->lrc[0]);

		/*
		 * Look for a matching GuC reported error capture node from
		 * the internal output link-list based on engine, guc id and
		 * lrca info.
		 */
		list_for_each_entry_safe(n, ntmp, &guc->capture->outlist, link) {
			if (n->eng_class == guc_class && n->eng_inst == hwe->instance &&
			    n->guc_id == guc_id && n->lrca == lrca &&
			    n->source == XE_ENGINE_CAPTURE_SOURCE_GUC) {
				n->locked = 1;
				return n;
			}
		}
	}
	return NULL;
}

/**
 * xe_engine_snapshot_capture_for_queue - Take snapshot of associated engine
 * @q: The exec queue object
 *
 * Take snapshot of associated HW Engine
 *
 * Returns: None.
 */
void
xe_engine_snapshot_capture_for_queue(struct xe_exec_queue *q)
{
	struct xe_device *xe = gt_to_xe(q->gt);
	struct xe_devcoredump *coredump = &xe->devcoredump;
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	u32 adj_logical_mask = q->logical_mask;

	if (IS_SRIOV_VF(xe))
		return;

	for_each_hw_engine(hwe, q->gt, id) {
		if (hwe->class != q->hwe->class ||
		    !(BIT(hwe->logical_instance) & adj_logical_mask)) {
			coredump->snapshot.hwe[id] = NULL;
			continue;
		}

		if (!coredump->snapshot.hwe[id]) {
			coredump->snapshot.hwe[id] =
				xe_hw_engine_snapshot_capture(hwe, q);
		} else {
			struct __guc_capture_parsed_output *new;

			new = xe_guc_capture_get_matching_and_lock(q);
			if (new) {
				struct xe_guc *guc =  &q->gt->uc.guc;

				/*
				 * If we are in here, it means we found a fresh
				 * GuC-err-capture node for this engine after
				 * previously failing to find a match in the
				 * early part of guc_exec_queue_timedout_job.
				 * Thus we must free the manually captured node
				 */
				guc_capture_free_outlist_node(guc->capture,
							      coredump->snapshot.matched_node);
				coredump->snapshot.matched_node = new;
			}
		}

		break;
	}
}

/*
 * xe_guc_capture_put_matched_nodes - Cleanup macthed nodes
 * @guc: The GuC object
 *
 * Free matched node and all nodes with the equal guc_id from
 * GuC captured outlist
 */
void xe_guc_capture_put_matched_nodes(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_devcoredump *devcoredump = &xe->devcoredump;
	struct __guc_capture_parsed_output *n = devcoredump->snapshot.matched_node;

	if (n) {
		guc_capture_remove_stale_matches_from_list(guc->capture, n);
		guc_capture_free_outlist_node(guc->capture, n);
		devcoredump->snapshot.matched_node = NULL;
	}
}

/*
 * xe_guc_capture_steered_list_init - Init steering register list
 * @guc: The GuC object
 *
 * Init steering register list for GuC register capture, create pre-alloc node
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
	check_guc_capture_size(guc);
	guc_capture_create_prealloc_nodes(guc);
}

/*
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

	INIT_LIST_HEAD(&guc->capture->outlist);
	INIT_LIST_HEAD(&guc->capture->cachelist);

	return 0;
}

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/drm_managed.h>
#include <drm/xe_drm.h>

#include "regs/xe_oa_regs.h"
#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_oa.h"
#include "xe_perf.h"

#define XE_OA_UNIT_INVALID U32_MAX

struct xe_oa_reg {
	struct xe_reg addr;
	u32 value;
};

struct xe_oa_config {
	struct xe_oa *oa;

	char uuid[UUID_STRING_LEN + 1];
	int id;

	const struct xe_oa_reg *regs;
	u32 regs_len;

	struct attribute_group sysfs_metric;
	struct attribute *attrs[2];
	struct kobj_attribute sysfs_metric_id;

	struct kref ref;
	struct rcu_head rcu;
};

#define DRM_FMT(x) DRM_XE_OA_FMT_TYPE_##x

static const struct xe_oa_format oa_formats[] = {
	[XE_OA_FORMAT_C4_B8]			= { 7, 64,  DRM_FMT(OAG) },
	[XE_OA_FORMAT_A12]			= { 0, 64,  DRM_FMT(OAG) },
	[XE_OA_FORMAT_A12_B8_C8]		= { 2, 128, DRM_FMT(OAG) },
	[XE_OA_FORMAT_A32u40_A4u32_B8_C8]	= { 5, 256, DRM_FMT(OAG) },
	[XE_OAR_FORMAT_A32u40_A4u32_B8_C8]	= { 5, 256, DRM_FMT(OAR) },
	[XE_OA_FORMAT_A24u40_A14u32_B8_C8]	= { 5, 256, DRM_FMT(OAG) },
	[XE_OAC_FORMAT_A24u64_B8_C8]		= { 1, 320, DRM_FMT(OAC), HDR_64_BIT },
	[XE_OAC_FORMAT_A22u32_R2u32_B8_C8]	= { 2, 192, DRM_FMT(OAC), HDR_64_BIT },
	[XE_OAM_FORMAT_MPEC8u64_B8_C8]		= { 1, 192, DRM_FMT(OAM_MPEC), HDR_64_BIT },
	[XE_OAM_FORMAT_MPEC8u32_B8_C8]		= { 2, 128, DRM_FMT(OAM_MPEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC64u64]			= { 1, 576, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC64u64_B8_C8]		= { 1, 640, DRM_FMT(PEC), HDR_64_BIT, 1, 1 },
	[XE_OA_FORMAT_PEC64u32]			= { 1, 320, DRM_FMT(PEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC32u64_G1]		= { 5, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC32u32_G1]		= { 5, 192, DRM_FMT(PEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC32u64_G2]		= { 6, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC32u32_G2]		= { 6, 192, DRM_FMT(PEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC36u64_G1_32_G2_4]	= { 3, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC36u64_G1_4_G2_32]	= { 4, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
};

static void xe_oa_config_release(struct kref *ref)
{
	struct xe_oa_config *oa_config =
		container_of(ref, typeof(*oa_config), ref);

	kfree(oa_config->regs);

	kfree_rcu(oa_config, rcu);
}

static void xe_oa_config_put(struct xe_oa_config *oa_config)
{
	if (!oa_config)
		return;

	kref_put(&oa_config->ref, xe_oa_config_release);
}

static bool xe_oa_is_valid_flex_addr(struct xe_oa *oa, u32 addr)
{
	static const struct xe_reg flex_eu_regs[] = {
		EU_PERF_CNTL0,
		EU_PERF_CNTL1,
		EU_PERF_CNTL2,
		EU_PERF_CNTL3,
		EU_PERF_CNTL4,
		EU_PERF_CNTL5,
		EU_PERF_CNTL6,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(flex_eu_regs); i++) {
		if (flex_eu_regs[i].addr == addr)
			return true;
	}
	return false;
}

static bool xe_oa_reg_in_range_table(u32 addr, const struct xe_mmio_range *table)
{
	while (table->start && table->end) {
		if (addr >= table->start && addr <= table->end)
			return true;

		table++;
	}

	return false;
}

static const struct xe_mmio_range xehp_oa_b_counters[] = {
	{ .start = 0xdc48, .end = 0xdc48 },	/* OAA_ENABLE_REG */
	{ .start = 0xdd00, .end = 0xdd48 },	/* OAG_LCE0_0 - OAA_LENABLE_REG */
	{}
};

static const struct xe_mmio_range gen12_oa_b_counters[] = {
	{ .start = 0x2b2c, .end = 0x2b2c },	/* OAG_OA_PESS */
	{ .start = 0xd900, .end = 0xd91c },	/* OAG_OASTARTTRIG[1-8] */
	{ .start = 0xd920, .end = 0xd93c },	/* OAG_OAREPORTTRIG1[1-8] */
	{ .start = 0xd940, .end = 0xd97c },	/* OAG_CEC[0-7][0-1] */
	{ .start = 0xdc00, .end = 0xdc3c },	/* OAG_SCEC[0-7][0-1] */
	{ .start = 0xdc40, .end = 0xdc40 },	/* OAG_SPCTR_CNF */
	{ .start = 0xdc44, .end = 0xdc44 },	/* OAA_DBG_REG */
	{}
};

static const struct xe_mmio_range mtl_oam_b_counters[] = {
	{ .start = 0x393000, .end = 0x39301c },	/* OAM_STARTTRIG1[1-8] */
	{ .start = 0x393020, .end = 0x39303c },	/* OAM_REPORTTRIG1[1-8] */
	{ .start = 0x393040, .end = 0x39307c },	/* OAM_CEC[0-7][0-1] */
	{ .start = 0x393200, .end = 0x39323C },	/* MPES[0-7] */
	{}
};

static const struct xe_mmio_range xe2_oa_b_counters[] = {
	{ .start = 0x393200, .end = 0x39323C },	/* MPES_0_MPES_SAG - MPES_7_UPPER_MPES_SAG */
	{ .start = 0x394200, .end = 0x39423C },	/* MPES_0_MPES_SCMI0 - MPES_7_UPPER_MPES_SCMI0 */
	{ .start = 0x394A00, .end = 0x394A3C },	/* MPES_0_MPES_SCMI1 - MPES_7_UPPER_MPES_SCMI1 */
	{},
};

static bool xe_oa_is_valid_b_counter_addr(struct xe_oa *oa, u32 addr)
{
	return xe_oa_reg_in_range_table(addr, xehp_oa_b_counters) ||
		xe_oa_reg_in_range_table(addr, gen12_oa_b_counters) ||
		xe_oa_reg_in_range_table(addr, mtl_oam_b_counters) ||
		(GRAPHICS_VER(oa->xe) >= 20 &&
		 xe_oa_reg_in_range_table(addr, xe2_oa_b_counters));
}

static const struct xe_mmio_range mtl_oa_mux_regs[] = {
	{ .start = 0x0d00, .end = 0x0d04 },	/* RPM_CONFIG[0-1] */
	{ .start = 0x0d0c, .end = 0x0d2c },	/* NOA_CONFIG[0-8] */
	{ .start = 0x9840, .end = 0x9840 },	/* GDT_CHICKEN_BITS */
	{ .start = 0x9884, .end = 0x9888 },	/* NOA_WRITE */
	{ .start = 0x38d100, .end = 0x38d114},	/* VISACTL */
	{}
};

static const struct xe_mmio_range gen12_oa_mux_regs[] = {
	{ .start = 0x0d00, .end = 0x0d04 },     /* RPM_CONFIG[0-1] */
	{ .start = 0x0d0c, .end = 0x0d2c },     /* NOA_CONFIG[0-8] */
	{ .start = 0x9840, .end = 0x9840 },	/* GDT_CHICKEN_BITS */
	{ .start = 0x9884, .end = 0x9888 },	/* NOA_WRITE */
	{ .start = 0x20cc, .end = 0x20cc },	/* WAIT_FOR_RC6_EXIT */
	{}
};

static const struct xe_mmio_range xe2_oa_mux_regs[] = {
	{ .start = 0x5194, .end = 0x5194 },	/* SYS_MEM_LAT_MEASURE_MERTF_GRP_3D */
	{ .start = 0x8704, .end = 0x8704 },	/* LMEM_LAT_MEASURE_MCFG_GRP */
	{ .start = 0xB1BC, .end = 0xB1BC },	/* L3_BANK_LAT_MEASURE_LBCF_GFX */
	{ .start = 0xE18C, .end = 0xE18C },	/* SAMPLER_MODE */
	{ .start = 0xE590, .end = 0xE590 },	/* TDL_LSC_LAT_MEASURE_TDL_GFX */
	{ .start = 0x13000, .end = 0x137FC },	/* PES_0_PESL0 - PES_63_UPPER_PESL3 */
	{},
};

static bool xe_oa_is_valid_mux_addr(struct xe_oa *oa, u32 addr)
{
	if (GRAPHICS_VER(oa->xe) >= 20)
		return xe_oa_reg_in_range_table(addr, xe2_oa_mux_regs);
	else if (GRAPHICS_VERx100(oa->xe) >= 1270)
		return xe_oa_reg_in_range_table(addr, mtl_oa_mux_regs);
	else
		return xe_oa_reg_in_range_table(addr, gen12_oa_mux_regs);
}

static bool xe_oa_is_valid_config_reg_addr(struct xe_oa *oa, u32 addr)
{
	return xe_oa_is_valid_flex_addr(oa, addr) ||
		xe_oa_is_valid_b_counter_addr(oa, addr) ||
		xe_oa_is_valid_mux_addr(oa, addr);
}

static struct xe_oa_reg *
xe_oa_alloc_regs(struct xe_oa *oa, bool (*is_valid)(struct xe_oa *oa, u32 addr),
		 u32 __user *regs, u32 n_regs)
{
	struct xe_oa_reg *oa_regs;
	int err;
	u32 i;

	oa_regs = kmalloc_array(n_regs, sizeof(*oa_regs), GFP_KERNEL);
	if (!oa_regs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < n_regs; i++) {
		u32 addr, value;

		err = get_user(addr, regs);
		if (err)
			goto addr_err;

		if (!is_valid(oa, addr)) {
			drm_dbg(&oa->xe->drm, "Invalid oa_reg address: %X\n", addr);
			err = -EINVAL;
			goto addr_err;
		}

		err = get_user(value, regs + 1);
		if (err)
			goto addr_err;

		oa_regs[i].addr = XE_REG(addr);
		oa_regs[i].value = value;

		regs += 2;
	}

	return oa_regs;

addr_err:
	kfree(oa_regs);
	return ERR_PTR(err);
}

static ssize_t show_dynamic_id(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       char *buf)
{
	struct xe_oa_config *oa_config =
		container_of(attr, typeof(*oa_config), sysfs_metric_id);

	return sysfs_emit(buf, "%d\n", oa_config->id);
}

static int create_dynamic_oa_sysfs_entry(struct xe_oa *oa,
					 struct xe_oa_config *oa_config)
{
	sysfs_attr_init(&oa_config->sysfs_metric_id.attr);
	oa_config->sysfs_metric_id.attr.name = "id";
	oa_config->sysfs_metric_id.attr.mode = 0444;
	oa_config->sysfs_metric_id.show = show_dynamic_id;
	oa_config->sysfs_metric_id.store = NULL;

	oa_config->attrs[0] = &oa_config->sysfs_metric_id.attr;
	oa_config->attrs[1] = NULL;

	oa_config->sysfs_metric.name = oa_config->uuid;
	oa_config->sysfs_metric.attrs = oa_config->attrs;

	return sysfs_create_group(oa->metrics_kobj, &oa_config->sysfs_metric);
}

/**
 * xe_oa_add_config_ioctl - Adds one OA config
 * @dev: @drm_device
 * @data: pointer to struct @drm_xe_oa_config
 * @file: @drm_file
 *
 * The functions adds an OA config to the set of OA configs maintained in
 * the kernel. The config determines which OA metrics are collected for an
 * OA stream.
 */
int xe_oa_add_config_ioctl(struct drm_device *dev, u64 data, struct drm_file *file)
{
	struct xe_oa *oa = &to_xe_device(dev)->oa;
	struct drm_xe_oa_config param;
	struct drm_xe_oa_config *arg = &param;
	struct xe_oa_config *oa_config, *tmp;
	struct xe_oa_reg *regs;
	int err, id;

	if (!oa->xe) {
		drm_dbg(&oa->xe->drm, "xe oa interface not available for this system\n");
		return -ENODEV;
	}

	if (xe_perf_stream_paranoid && !perfmon_capable()) {
		drm_dbg(&oa->xe->drm, "Insufficient privileges to add xe OA config\n");
		return -EACCES;
	}

	err = __copy_from_user(&param, u64_to_user_ptr(data), sizeof(param));
	if (XE_IOCTL_DBG(oa->xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(oa->xe, arg->extensions) ||
	    XE_IOCTL_DBG(oa->xe, !arg->regs_ptr) ||
	    XE_IOCTL_DBG(oa->xe, !arg->n_regs))
		return -EINVAL;

	oa_config = kzalloc(sizeof(*oa_config), GFP_KERNEL);
	if (!oa_config)
		return -ENOMEM;

	oa_config->oa = oa;
	kref_init(&oa_config->ref);

	if (!uuid_is_valid(arg->uuid)) {
		drm_dbg(&oa->xe->drm, "Invalid uuid format for OA config\n");
		err = -EINVAL;
		goto reg_err;
	}

	/* Last character in oa_config->uuid will be 0 because oa_config is kzalloc */
	memcpy(oa_config->uuid, arg->uuid, sizeof(arg->uuid));

	oa_config->regs_len = arg->n_regs;
	regs = xe_oa_alloc_regs(oa, xe_oa_is_valid_config_reg_addr,
				u64_to_user_ptr(arg->regs_ptr),
				arg->n_regs);
	if (IS_ERR(regs)) {
		drm_dbg(&oa->xe->drm, "Failed to create OA config for mux_regs\n");
		err = PTR_ERR(regs);
		goto reg_err;
	}
	oa_config->regs = regs;

	err = mutex_lock_interruptible(&oa->metrics_lock);
	if (err)
		goto reg_err;

	/* We shouldn't have too many configs, so this iteration shouldn't be too costly */
	idr_for_each_entry(&oa->metrics_idr, tmp, id) {
		if (!strcmp(tmp->uuid, oa_config->uuid)) {
			drm_dbg(&oa->xe->drm, "OA config already exists with this uuid\n");
			err = -EADDRINUSE;
			goto sysfs_err;
		}
	}

	err = create_dynamic_oa_sysfs_entry(oa, oa_config);
	if (err) {
		drm_dbg(&oa->xe->drm, "Failed to create sysfs entry for OA config\n");
		goto sysfs_err;
	}

	oa_config->id = idr_alloc(&oa->metrics_idr, oa_config, 1, 0, GFP_KERNEL);
	if (oa_config->id < 0) {
		drm_dbg(&oa->xe->drm, "Failed to create sysfs entry for OA config\n");
		err = oa_config->id;
		goto sysfs_err;
	}

	mutex_unlock(&oa->metrics_lock);

	drm_dbg(&oa->xe->drm, "Added config %s id=%i\n", oa_config->uuid, oa_config->id);

	return oa_config->id;

sysfs_err:
	mutex_unlock(&oa->metrics_lock);
reg_err:
	xe_oa_config_put(oa_config);
	drm_dbg(&oa->xe->drm, "Failed to add new OA config\n");
	return err;
}

/**
 * xe_oa_remove_config_ioctl - Removes one OA config
 * @dev: @drm_device
 * @data: pointer to struct @drm_xe_perf_param
 * @file: @drm_file
 */
int xe_oa_remove_config_ioctl(struct drm_device *dev, u64 data, struct drm_file *file)
{
	struct xe_oa *oa = &to_xe_device(dev)->oa;
	struct xe_oa_config *oa_config;
	u64 arg, *ptr = u64_to_user_ptr(data);
	int ret;

	if (!oa->xe) {
		drm_dbg(&oa->xe->drm, "xe oa interface not available for this system\n");
		return -ENODEV;
	}

	if (xe_perf_stream_paranoid && !perfmon_capable()) {
		drm_dbg(&oa->xe->drm, "Insufficient privileges to remove xe OA config\n");
		return -EACCES;
	}

	ret = get_user(arg, ptr);
	if (XE_IOCTL_DBG(oa->xe, ret))
		return ret;

	ret = mutex_lock_interruptible(&oa->metrics_lock);
	if (ret)
		return ret;

	oa_config = idr_find(&oa->metrics_idr, arg);
	if (!oa_config) {
		drm_dbg(&oa->xe->drm, "Failed to remove unknown OA config\n");
		ret = -ENOENT;
		goto err_unlock;
	}

	WARN_ON(arg != oa_config->id);

	sysfs_remove_group(oa->metrics_kobj, &oa_config->sysfs_metric);
	idr_remove(&oa->metrics_idr, arg);

	mutex_unlock(&oa->metrics_lock);

	drm_dbg(&oa->xe->drm, "Removed config %s id=%i\n", oa_config->uuid, oa_config->id);

	xe_oa_config_put(oa_config);

	return 0;

err_unlock:
	mutex_unlock(&oa->metrics_lock);
	return ret;
}

/**
 * xe_oa_register - Xe OA registration
 * @xe: @xe_device
 *
 * Exposes the metrics sysfs directory upon completion of module initialization
 */
void xe_oa_register(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;

	if (!oa->xe)
		return;

	oa->metrics_kobj = kobject_create_and_add("metrics",
						  &xe->drm.primary->kdev->kobj);
}

/**
 * xe_oa_unregister - Xe OA de-registration
 * @xe: @xe_device
 */
void xe_oa_unregister(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;

	if (!oa->metrics_kobj)
		return;

	kobject_put(oa->metrics_kobj);
	oa->metrics_kobj = NULL;
}

static u32 num_oa_units_per_gt(struct xe_gt *gt)
{
	return 1;
}

static u32 __hwe_oam_unit(struct xe_hw_engine *hwe)
{
	if (GRAPHICS_VERx100(gt_to_xe(hwe->gt)) >= 1270) {
		/*
		 * There's 1 SAMEDIA gt and 1 OAM per SAMEDIA gt. All media slices
		 * within the gt use the same OAM. All MTL/LNL SKUs list 1 SA MEDIA
		 */
		xe_gt_WARN_ON(hwe->gt, hwe->gt->info.type != XE_GT_TYPE_MEDIA);

		return 0;
	}

	return XE_OA_UNIT_INVALID;
}

static u32 __hwe_oa_unit(struct xe_hw_engine *hwe)
{
	switch (hwe->class) {
	case XE_ENGINE_CLASS_RENDER:
	case XE_ENGINE_CLASS_COMPUTE:
		return 0;

	case XE_ENGINE_CLASS_VIDEO_DECODE:
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		return __hwe_oam_unit(hwe);

	default:
		return XE_OA_UNIT_INVALID;
	}
}

static struct xe_oa_regs __oam_regs(u32 base)
{
	return (struct xe_oa_regs) {
		base,
		OAM_HEAD_POINTER(base),
		OAM_TAIL_POINTER(base),
		OAM_BUFFER(base),
		OAM_CONTEXT_CONTROL(base),
		OAM_CONTROL(base),
		OAM_DEBUG(base),
		OAM_STATUS(base),
		OAM_CONTROL_COUNTER_SEL_MASK,
	};
}

static struct xe_oa_regs __oag_regs(void)
{
	return (struct xe_oa_regs) {
		0,
		OAG_OAHEADPTR,
		OAG_OATAILPTR,
		OAG_OABUFFER,
		OAG_OAGLBCTXCTRL,
		OAG_OACONTROL,
		OAG_OA_DEBUG,
		OAG_OASTATUS,
		OAG_OACONTROL_OA_COUNTER_SEL_MASK,
	};
}

static void __xe_oa_init_oa_units(struct xe_gt *gt)
{
	const u32 mtl_oa_base[] = { 0x13000 };
	int i, num_units = gt->oa.num_oa_units;

	for (i = 0; i < num_units; i++) {
		struct xe_oa_unit *u = &gt->oa.oa_unit[i];

		if (gt->info.type != XE_GT_TYPE_MEDIA) {
			u->regs = __oag_regs();
			u->type = DRM_XE_OA_UNIT_TYPE_OAG;
		} else if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1270) {
			u->regs = __oam_regs(mtl_oa_base[i]);
			u->type = DRM_XE_OA_UNIT_TYPE_OAM;
		}

		/* Set oa_unit_ids now to ensure ids remain contiguous */
		u->oa_unit_id = gt_to_xe(gt)->oa.oa_unit_ids++;
	}
}

static int xe_oa_init_gt(struct xe_gt *gt)
{
	u32 num_oa_units = num_oa_units_per_gt(gt);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct xe_oa_unit *u;

	u = drmm_kcalloc(&gt_to_xe(gt)->drm, num_oa_units, sizeof(*u), GFP_KERNEL);
	if (!u)
		return -ENOMEM;

	for_each_hw_engine(hwe, gt, id) {
		u32 index = __hwe_oa_unit(hwe);

		hwe->oa_unit = NULL;
		if (index < num_oa_units) {
			u[index].num_engines++;
			hwe->oa_unit = &u[index];
		}
	}

	/*
	 * Fused off engines can result in oa_unit's with num_engines == 0. These units
	 * will appear in OA unit query, but no perf streams can be opened on them.
	 */
	gt->oa.num_oa_units = num_oa_units;
	gt->oa.oa_unit = u;

	__xe_oa_init_oa_units(gt);

	drmm_mutex_init(&gt_to_xe(gt)->drm, &gt->oa.gt_lock);

	return 0;
}

static int xe_oa_init_oa_units(struct xe_oa *oa)
{
	struct xe_gt *gt;
	int i, ret;

	for_each_gt(gt, oa->xe, i) {
		ret = xe_oa_init_gt(gt);
		if (ret)
			return ret;
	}

	return 0;
}

static void oa_format_add(struct xe_oa *oa, enum xe_oa_format_name format)
{
	__set_bit(format, oa->format_mask);
}

static void xe_oa_init_supported_formats(struct xe_oa *oa)
{
	if (GRAPHICS_VER(oa->xe) >= 20) {
		/* Xe2+ */
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u64_B8_C8);
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_PEC64u64);
		oa_format_add(oa, XE_OA_FORMAT_PEC64u64_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_PEC64u32);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u64_G1);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u32_G1);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u64_G2);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u32_G2);
		oa_format_add(oa, XE_OA_FORMAT_PEC36u64_G1_32_G2_4);
		oa_format_add(oa, XE_OA_FORMAT_PEC36u64_G1_4_G2_32);
	} else if (GRAPHICS_VERx100(oa->xe) >= 1270) {
		/* XE_METEORLAKE */
		oa_format_add(oa, XE_OAR_FORMAT_A32u40_A4u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_A24u40_A14u32_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A24u64_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A22u32_R2u32_B8_C8);
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u64_B8_C8);
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u32_B8_C8);
	} else if (GRAPHICS_VERx100(oa->xe) >= 1255) {
		/* XE_DG2, XE_PVC */
		oa_format_add(oa, XE_OAR_FORMAT_A32u40_A4u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_A24u40_A14u32_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A24u64_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A22u32_R2u32_B8_C8);
	} else {
		/* Gen12+ */
		xe_assert(oa->xe, GRAPHICS_VER(oa->xe) >= 12);
		oa_format_add(oa, XE_OA_FORMAT_A12);
		oa_format_add(oa, XE_OA_FORMAT_A12_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_A32u40_A4u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_C4_B8);
	}
}

/**
 * xe_oa_init - OA initialization during device probe
 * @xe: @xe_device
 *
 * Return: 0 on success or a negative error code on failure
 */
int xe_oa_init(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;
	int ret;

	/* Support OA only with GuC submission and Gen12+ */
	if (XE_WARN_ON(!xe_device_uc_enabled(xe)) || XE_WARN_ON(GRAPHICS_VER(xe) < 12))
		return 0;

	oa->xe = xe;
	oa->oa_formats = oa_formats;

	drmm_mutex_init(&oa->xe->drm, &oa->metrics_lock);
	idr_init_base(&oa->metrics_idr, 1);

	ret = xe_oa_init_oa_units(oa);
	if (ret) {
		drm_err(&xe->drm, "OA initialization failed (%pe)\n", ERR_PTR(ret));
		goto exit;
	}

	xe_oa_init_supported_formats(oa);
	return 0;
exit:
	oa->xe = NULL;
	return ret;
}

static int destroy_config(int id, void *p, void *data)
{
	xe_oa_config_put(p);
	return 0;
}

/**
 * xe_oa_fini - OA de-initialization during device remove
 * @xe: @xe_device
 */
void xe_oa_fini(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;

	if (!oa->xe)
		return;

	idr_for_each(&oa->metrics_idr, destroy_config, oa);
	idr_destroy(&oa->metrics_idr);

	oa->xe = NULL;
}

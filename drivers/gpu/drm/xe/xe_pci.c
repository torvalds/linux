// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_pci.h"

#include <linux/device/driver.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>

#include <drm/drm_color_mgmt.h>
#include <drm/drm_drv.h>
#include <drm/xe_pciids.h>

#include "regs/xe_regs.h"
#include "xe_device.h"
#include "xe_drv.h"
#include "xe_macros.h"
#include "xe_module.h"
#include "xe_pm.h"
#include "xe_step.h"

#define DEV_INFO_FOR_EACH_FLAG(func) \
	func(require_force_probe); \
	func(is_dgfx); \
	/* Keep has_* in alphabetical order */ \

struct xe_subplatform_desc {
	enum xe_subplatform subplatform;
	const char *name;
	const u16 *pciidlist;
};

struct xe_gt_desc {
	enum xe_gt_type type;
	u8 vram_id;
	u64 engine_mask;
	u32 mmio_adj_limit;
	u32 mmio_adj_offset;
};

struct xe_device_desc {
	u8 graphics_ver;
	u8 graphics_rel;
	u8 media_ver;
	u8 media_rel;

	u64 platform_engine_mask; /* Engines supported by the HW */

	enum xe_platform platform;
	const char *platform_name;
	const struct xe_subplatform_desc *subplatforms;
	const struct xe_gt_desc *extra_gts;

	u8 dma_mask_size; /* available DMA address bits */

	u8 gt; /* GT number, 0 if undefined */

#define DEFINE_FLAG(name) u8 name:1
	DEV_INFO_FOR_EACH_FLAG(DEFINE_FLAG);
#undef DEFINE_FLAG

	u8 vram_flags;
	u8 max_tiles;
	u8 vm_max_level;

	bool supports_usm;
	bool has_flat_ccs;
	bool has_4tile;
	bool has_range_tlb_invalidation;
	bool has_asid;
	bool has_link_copy_engine;
};

#define PLATFORM(x)		\
	.platform = (x),	\
	.platform_name = #x

#define NOP(x)	x

/* Keep in gen based order, and chronological order within a gen */
#define GEN12_FEATURES \
	.require_force_probe = true, \
	.graphics_ver = 12, \
	.media_ver = 12, \
	.dma_mask_size = 39, \
	.max_tiles = 1, \
	.vm_max_level = 3, \
	.vram_flags = 0

static const struct xe_device_desc tgl_desc = {
	GEN12_FEATURES,
	PLATFORM(XE_TIGERLAKE),
	.platform_engine_mask =
		BIT(XE_HW_ENGINE_RCS0) | BIT(XE_HW_ENGINE_BCS0) |
		BIT(XE_HW_ENGINE_VECS0) | BIT(XE_HW_ENGINE_VCS0) |
		BIT(XE_HW_ENGINE_VCS2),
};

static const struct xe_device_desc adl_s_desc = {
	GEN12_FEATURES,
	PLATFORM(XE_ALDERLAKE_S),
	.platform_engine_mask =
		BIT(XE_HW_ENGINE_RCS0) | BIT(XE_HW_ENGINE_BCS0) |
		BIT(XE_HW_ENGINE_VECS0) | BIT(XE_HW_ENGINE_VCS0) |
		BIT(XE_HW_ENGINE_VCS2),
};

static const u16 adlp_rplu_ids[] = { XE_RPLU_IDS(NOP), 0 };

static const struct xe_device_desc adl_p_desc = {
	GEN12_FEATURES,
	PLATFORM(XE_ALDERLAKE_P),
	.platform_engine_mask =
		BIT(XE_HW_ENGINE_RCS0) | BIT(XE_HW_ENGINE_BCS0) |
		BIT(XE_HW_ENGINE_VECS0) | BIT(XE_HW_ENGINE_VCS0) |
		BIT(XE_HW_ENGINE_VCS2),
	.subplatforms = (const struct xe_subplatform_desc[]) {
		{ XE_SUBPLATFORM_ADLP_RPLU, "RPLU", adlp_rplu_ids },
		{},
	},
};

#define DGFX_FEATURES \
	.is_dgfx = 1

static const struct xe_device_desc dg1_desc = {
	GEN12_FEATURES,
	DGFX_FEATURES,
	.graphics_rel = 10,
	PLATFORM(XE_DG1),
	.platform_engine_mask =
		BIT(XE_HW_ENGINE_RCS0) | BIT(XE_HW_ENGINE_BCS0) |
		BIT(XE_HW_ENGINE_VECS0) | BIT(XE_HW_ENGINE_VCS0) |
		BIT(XE_HW_ENGINE_VCS2),
};

#define XE_HP_FEATURES \
	.require_force_probe = true, \
	.graphics_ver = 12, \
	.graphics_rel = 50, \
	.has_range_tlb_invalidation = true, \
	.has_flat_ccs = true, \
	.dma_mask_size = 46, \
	.max_tiles = 1, \
	.vm_max_level = 3

#define XE_HPM_FEATURES \
	.media_ver = 12, \
	.media_rel = 50

static const u16 dg2_g10_ids[] = { XE_DG2_G10_IDS(NOP), XE_ATS_M150_IDS(NOP), 0 };
static const u16 dg2_g11_ids[] = { XE_DG2_G11_IDS(NOP), XE_ATS_M75_IDS(NOP), 0 };
static const u16 dg2_g12_ids[] = { XE_DG2_G12_IDS(NOP), 0 };

#define DG2_FEATURES \
	DGFX_FEATURES, \
	.graphics_rel = 55, \
	.media_rel = 55, \
	PLATFORM(XE_DG2), \
	.subplatforms = (const struct xe_subplatform_desc[]) { \
		{ XE_SUBPLATFORM_DG2_G10, "G10", dg2_g10_ids }, \
		{ XE_SUBPLATFORM_DG2_G11, "G11", dg2_g11_ids }, \
		{ XE_SUBPLATFORM_DG2_G12, "G12", dg2_g12_ids }, \
		{ } \
	}, \
	.platform_engine_mask = \
		BIT(XE_HW_ENGINE_RCS0) | BIT(XE_HW_ENGINE_BCS0) | \
		BIT(XE_HW_ENGINE_VECS0) | BIT(XE_HW_ENGINE_VECS1) | \
		BIT(XE_HW_ENGINE_VCS0) | BIT(XE_HW_ENGINE_VCS2) | \
		BIT(XE_HW_ENGINE_CCS0) | BIT(XE_HW_ENGINE_CCS1) | \
		BIT(XE_HW_ENGINE_CCS2) | BIT(XE_HW_ENGINE_CCS3), \
	.require_force_probe = true, \
	.vram_flags = XE_VRAM_FLAGS_NEED64K, \
	.has_4tile = 1

static const struct xe_device_desc ats_m_desc = {
	XE_HP_FEATURES,
	XE_HPM_FEATURES,

	DG2_FEATURES,
};

static const struct xe_device_desc dg2_desc = {
	XE_HP_FEATURES,
	XE_HPM_FEATURES,

	DG2_FEATURES,
};

#define PVC_ENGINES \
	BIT(XE_HW_ENGINE_BCS0) | BIT(XE_HW_ENGINE_BCS1) | \
	BIT(XE_HW_ENGINE_BCS2) | BIT(XE_HW_ENGINE_BCS3) | \
	BIT(XE_HW_ENGINE_BCS4) | BIT(XE_HW_ENGINE_BCS5) | \
	BIT(XE_HW_ENGINE_BCS6) | BIT(XE_HW_ENGINE_BCS7) | \
	BIT(XE_HW_ENGINE_BCS8) | \
	BIT(XE_HW_ENGINE_VCS0) | BIT(XE_HW_ENGINE_VCS1) | \
	BIT(XE_HW_ENGINE_VCS2) | \
	BIT(XE_HW_ENGINE_CCS0) | BIT(XE_HW_ENGINE_CCS1) | \
	BIT(XE_HW_ENGINE_CCS2) | BIT(XE_HW_ENGINE_CCS3)

static const struct xe_gt_desc pvc_gts[] = {
	{
		.type = XE_GT_TYPE_REMOTE,
		.vram_id = 1,
		.engine_mask = PVC_ENGINES,
		.mmio_adj_limit = 0,
		.mmio_adj_offset = 0,
	},
};

static const __maybe_unused struct xe_device_desc pvc_desc = {
	XE_HP_FEATURES,
	XE_HPM_FEATURES,
	DGFX_FEATURES,
	PLATFORM(XE_PVC),
	.extra_gts = pvc_gts,
	.graphics_rel = 60,
	.has_flat_ccs = 0,
	.media_rel = 60,
	.platform_engine_mask = PVC_ENGINES,
	.vram_flags = XE_VRAM_FLAGS_NEED64K,
	.dma_mask_size = 52,
	.max_tiles = 2,
	.vm_max_level = 4,
	.supports_usm = true,
	.has_asid = true,
	.has_link_copy_engine = true,
};

#define MTL_MEDIA_ENGINES \
	BIT(XE_HW_ENGINE_VCS0) | BIT(XE_HW_ENGINE_VCS2) | \
	BIT(XE_HW_ENGINE_VECS0)	/* TODO: GSC0 */

static const struct xe_gt_desc xelpmp_gts[] = {
	{
		.type = XE_GT_TYPE_MEDIA,
		.vram_id = 0,
		.engine_mask = MTL_MEDIA_ENGINES,
		.mmio_adj_limit = 0x40000,
		.mmio_adj_offset = 0x380000,
	},
};

#define MTL_MAIN_ENGINES \
	BIT(XE_HW_ENGINE_RCS0) | BIT(XE_HW_ENGINE_BCS0) | \
	BIT(XE_HW_ENGINE_CCS0)

static const struct xe_device_desc mtl_desc = {
	/*
	 * Real graphics IP version will be obtained from hardware GMD_ID
	 * register.  Value provided here is just for sanity checking.
	 */
	.require_force_probe = true,
	.graphics_ver = 12,
	.graphics_rel = 70,
	.dma_mask_size = 46,
	.max_tiles = 2,
	.vm_max_level = 3,
	.media_ver = 13,
	.has_range_tlb_invalidation = true,
	PLATFORM(XE_METEORLAKE),
	.extra_gts = xelpmp_gts,
	.platform_engine_mask = MTL_MAIN_ENGINES,
};

#undef PLATFORM

#define INTEL_VGA_DEVICE(id, info) {			\
	PCI_DEVICE(PCI_VENDOR_ID_INTEL, id),		\
	PCI_BASE_CLASS_DISPLAY << 16, 0xff << 16,	\
	(unsigned long) info }

/*
 * Make sure any device matches here are from most specific to most
 * general.  For example, since the Quanta match is based on the subsystem
 * and subvendor IDs, we need it to come before the more general IVB
 * PCI ID matches, otherwise we'll use the wrong info struct above.
 */
static const struct pci_device_id pciidlist[] = {
	XE_TGL_IDS(INTEL_VGA_DEVICE, &tgl_desc),
	XE_DG1_IDS(INTEL_VGA_DEVICE, &dg1_desc),
	XE_ATS_M_IDS(INTEL_VGA_DEVICE, &ats_m_desc),
	XE_DG2_IDS(INTEL_VGA_DEVICE, &dg2_desc),
	XE_ADLS_IDS(INTEL_VGA_DEVICE, &adl_s_desc),
	XE_ADLP_IDS(INTEL_VGA_DEVICE, &adl_p_desc),
	XE_MTL_IDS(INTEL_VGA_DEVICE, &mtl_desc),
	{ }
};
MODULE_DEVICE_TABLE(pci, pciidlist);

#undef INTEL_VGA_DEVICE

/* is device_id present in comma separated list of ids */
static bool device_id_in_list(u16 device_id, const char *devices, bool negative)
{
	char *s, *p, *tok;
	bool ret;

	if (!devices || !*devices)
		return false;

	/* match everything */
	if (negative && strcmp(devices, "!*") == 0)
		return true;
	if (!negative && strcmp(devices, "*") == 0)
		return true;

	s = kstrdup(devices, GFP_KERNEL);
	if (!s)
		return false;

	for (p = s, ret = false; (tok = strsep(&p, ",")) != NULL; ) {
		u16 val;

		if (negative && tok[0] == '!')
			tok++;
		else if ((negative && tok[0] != '!') ||
			 (!negative && tok[0] == '!'))
			continue;

		if (kstrtou16(tok, 16, &val) == 0 && val == device_id) {
			ret = true;
			break;
		}
	}

	kfree(s);

	return ret;
}

static bool id_forced(u16 device_id)
{
	return device_id_in_list(device_id, xe_param_force_probe, false);
}

static bool id_blocked(u16 device_id)
{
	return device_id_in_list(device_id, xe_param_force_probe, true);
}

static const struct xe_subplatform_desc *
subplatform_get(const struct xe_device *xe, const struct xe_device_desc *desc)
{
	const struct xe_subplatform_desc *sp;
	const u16 *id;

	for (sp = desc->subplatforms; sp && sp->subplatform; sp++)
		for (id = sp->pciidlist; *id; id++)
			if (*id == xe->info.devid)
				return sp;

	return NULL;
}

static void xe_pci_remove(struct pci_dev *pdev)
{
	struct xe_device *xe;

	xe = pci_get_drvdata(pdev);
	if (!xe) /* driver load aborted, nothing to cleanup */
		return;

	xe_device_remove(xe);
	xe_pm_runtime_fini(xe);
	pci_set_drvdata(pdev, NULL);
}

static int xe_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct xe_device_desc *desc = (void *)ent->driver_data;
	const struct xe_subplatform_desc *spd;
	struct xe_device *xe;
	struct xe_gt *gt;
	u8 id;
	int err;

	if (desc->require_force_probe && !id_forced(pdev->device)) {
		dev_info(&pdev->dev,
			 "Your graphics device %04x is not officially supported\n"
			 "by xe driver in this kernel version. To force Xe probe,\n"
			 "use xe.force_probe='%04x' and i915.force_probe='!%04x'\n"
			 "module parameters or CONFIG_DRM_XE_FORCE_PROBE='%04x' and\n"
			 "CONFIG_DRM_I915_FORCE_PROBE='!%04x' configuration options.\n",
			 pdev->device, pdev->device, pdev->device,
			 pdev->device, pdev->device);
		return -ENODEV;
	}

	if (id_blocked(pdev->device)) {
		dev_info(&pdev->dev, "Probe blocked for device [%04x:%04x].\n",
			 pdev->vendor, pdev->device);
		return -ENODEV;
	}

	xe = xe_device_create(pdev, ent);
	if (IS_ERR(xe))
		return PTR_ERR(xe);

	xe->info.graphics_verx100 = desc->graphics_ver * 100 +
				    desc->graphics_rel;
	xe->info.media_verx100 = desc->media_ver * 100 +
				 desc->media_rel;
	xe->info.is_dgfx = desc->is_dgfx;
	xe->info.platform = desc->platform;
	xe->info.dma_mask_size = desc->dma_mask_size;
	xe->info.vram_flags = desc->vram_flags;
	xe->info.tile_count = desc->max_tiles;
	xe->info.vm_max_level = desc->vm_max_level;
	xe->info.supports_usm = desc->supports_usm;
	xe->info.has_asid = desc->has_asid;
	xe->info.has_flat_ccs = desc->has_flat_ccs;
	xe->info.has_4tile = desc->has_4tile;
	xe->info.has_range_tlb_invalidation = desc->has_range_tlb_invalidation;
	xe->info.has_link_copy_engine = desc->has_link_copy_engine;

	spd = subplatform_get(xe, desc);
	xe->info.subplatform = spd ? spd->subplatform : XE_SUBPLATFORM_NONE;
	xe->info.step = xe_step_get(xe);

	for (id = 0; id < xe->info.tile_count; ++id) {
		gt = xe->gt + id;
		gt->info.id = id;
		gt->xe = xe;

		if (id == 0) {
			gt->info.type = XE_GT_TYPE_MAIN;
			gt->info.vram_id = id;
			gt->info.__engine_mask = desc->platform_engine_mask;
			gt->mmio.adj_limit = 0;
			gt->mmio.adj_offset = 0;
		} else {
			gt->info.type = desc->extra_gts[id - 1].type;
			gt->info.vram_id = desc->extra_gts[id - 1].vram_id;
			gt->info.__engine_mask =
				desc->extra_gts[id - 1].engine_mask;
			gt->mmio.adj_limit =
				desc->extra_gts[id - 1].mmio_adj_limit;
			gt->mmio.adj_offset =
				desc->extra_gts[id - 1].mmio_adj_offset;
		}
	}

	drm_dbg(&xe->drm, "%s %s %04x:%04x dgfx:%d gfx100:%d media100:%d dma_m_s:%d tc:%d",
		desc->platform_name, spd ? spd->name : "",
		xe->info.devid, xe->info.revid,
		xe->info.is_dgfx, xe->info.graphics_verx100,
		xe->info.media_verx100,
		xe->info.dma_mask_size, xe->info.tile_count);

	drm_dbg(&xe->drm, "Stepping = (G:%s, M:%s, D:%s, B:%s)\n",
		xe_step_name(xe->info.step.graphics),
		xe_step_name(xe->info.step.media),
		xe_step_name(xe->info.step.display),
		xe_step_name(xe->info.step.basedie));

	pci_set_drvdata(pdev, xe);
	err = pci_enable_device(pdev);
	if (err) {
		drm_dev_put(&xe->drm);
		return err;
	}

	pci_set_master(pdev);

	if (pci_enable_msi(pdev) < 0)
		drm_dbg(&xe->drm, "can't enable MSI");

	err = xe_device_probe(xe);
	if (err) {
		pci_disable_device(pdev);
		return err;
	}

	xe_pm_runtime_init(xe);

	return 0;
}

static void xe_pci_shutdown(struct pci_dev *pdev)
{
	xe_device_shutdown(pdev_to_xe_device(pdev));
}

#ifdef CONFIG_PM_SLEEP
static int xe_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int err;

	err = xe_pm_suspend(pdev_to_xe_device(pdev));
	if (err)
		return err;

	pci_save_state(pdev);
	pci_disable_device(pdev);

	err = pci_set_power_state(pdev, PCI_D3hot);
	if (err)
		return err;

	return 0;
}

static int xe_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int err;

	err = pci_set_power_state(pdev, PCI_D0);
	if (err)
		return err;

	pci_restore_state(pdev);

	err = pci_enable_device(pdev);
	if (err)
		return err;

	pci_set_master(pdev);

	err = xe_pm_resume(pdev_to_xe_device(pdev));
	if (err)
		return err;

	return 0;
}
#endif

static int xe_pci_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	int err;

	err = xe_pm_runtime_suspend(xe);
	if (err)
		return err;

	pci_save_state(pdev);

	if (xe->d3cold_allowed) {
		pci_disable_device(pdev);
		pci_ignore_hotplug(pdev);
		pci_set_power_state(pdev, PCI_D3cold);
	} else {
		pci_set_power_state(pdev, PCI_D3hot);
	}

	return 0;
}

static int xe_pci_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	int err;

	err = pci_set_power_state(pdev, PCI_D0);
	if (err)
		return err;

	pci_restore_state(pdev);

	if (xe->d3cold_allowed) {
		err = pci_enable_device(pdev);
		if (err)
			return err;

		pci_set_master(pdev);
	}

	return xe_pm_runtime_resume(xe);
}

static int xe_pci_runtime_idle(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);

	/*
	 * FIXME: d3cold should be allowed (true) if
	 * (IS_DGFX(xe) && !xe_device_mem_access_ongoing(xe))
	 * however the change to the buddy allocator broke the
	 * xe_bo_restore_kernel when the pci device is disabled
	 */
	 xe->d3cold_allowed = false;

	return 0;
}

static const struct dev_pm_ops xe_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xe_pci_suspend, xe_pci_resume)
	SET_RUNTIME_PM_OPS(xe_pci_runtime_suspend, xe_pci_runtime_resume, xe_pci_runtime_idle)
};

static struct pci_driver xe_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = xe_pci_probe,
	.remove = xe_pci_remove,
	.shutdown = xe_pci_shutdown,
	.driver.pm = &xe_pm_ops,
};

int xe_register_pci_driver(void)
{
	return pci_register_driver(&xe_pci_driver);
}

void xe_unregister_pci_driver(void)
{
	pci_unregister_driver(&xe_pci_driver);
}

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
struct kunit_test_data {
	int ndevs;
	xe_device_fn xe_fn;
};

static int dev_to_xe_device_fn(struct device *dev, void *__data)

{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct kunit_test_data *data = __data;
	int ret = 0;
	int idx;

	data->ndevs++;

	if (drm_dev_enter(drm, &idx))
		ret = data->xe_fn(to_xe_device(dev_get_drvdata(dev)));
	drm_dev_exit(idx);

	return ret;
}

/**
 * xe_call_for_each_device - Iterate over all devices this driver binds to
 * @xe_fn: Function to call for each device.
 *
 * This function iterated over all devices this driver binds to, and calls
 * @xe_fn: for each one of them. If the called function returns anything else
 * than 0, iteration is stopped and the return value is returned by this
 * function. Across each function call, drm_dev_enter() / drm_dev_exit() is
 * called for the corresponding drm device.
 *
 * Return: Zero or the error code of a call to @xe_fn returning an error
 * code.
 */
int xe_call_for_each_device(xe_device_fn xe_fn)
{
	int ret;
	struct kunit_test_data data = {
	    .xe_fn = xe_fn,
	    .ndevs = 0,
	};

	ret = driver_for_each_device(&xe_pci_driver.driver, NULL,
				     &data, dev_to_xe_device_fn);

	if (!data.ndevs)
		kunit_skip(current->kunit_test, "test runs only on hardware\n");

	return ret;
}
#endif

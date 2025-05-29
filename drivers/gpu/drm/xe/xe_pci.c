// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_pci.h"

#include <kunit/static_stub.h>
#include <linux/device/driver.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>

#include <drm/drm_color_mgmt.h>
#include <drm/drm_drv.h>
#include <drm/intel/pciids.h>

#include "display/xe_display.h"
#include "regs/xe_gt_regs.h"
#include "xe_device.h"
#include "xe_drv.h"
#include "xe_gt.h"
#include "xe_gt_sriov_vf.h"
#include "xe_guc.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_module.h"
#include "xe_pci_sriov.h"
#include "xe_pci_types.h"
#include "xe_pm.h"
#include "xe_sriov.h"
#include "xe_step.h"
#include "xe_survivability_mode.h"
#include "xe_tile.h"

enum toggle_d3cold {
	D3COLD_DISABLE,
	D3COLD_ENABLE,
};

struct xe_subplatform_desc {
	enum xe_subplatform subplatform;
	const char *name;
	const u16 *pciidlist;
};

struct xe_device_desc {
	/* Should only ever be set for platforms without GMD_ID */
	const struct xe_ip *pre_gmdid_graphics_ip;
	/* Should only ever be set for platforms without GMD_ID */
	const struct xe_ip *pre_gmdid_media_ip;

	const char *platform_name;
	const struct xe_subplatform_desc *subplatforms;

	enum xe_platform platform;

	u8 dma_mask_size;
	u8 max_remote_tiles:2;

	u8 require_force_probe:1;
	u8 is_dgfx:1;

	u8 has_display:1;
	u8 has_fan_control:1;
	u8 has_heci_gscfi:1;
	u8 has_heci_cscfi:1;
	u8 has_llc:1;
	u8 has_mbx_power_limits:1;
	u8 has_pxp:1;
	u8 has_sriov:1;
	u8 needs_scratch:1;
	u8 skip_guc_pc:1;
	u8 skip_mtcfg:1;
	u8 skip_pcode:1;
};

__diag_push();
__diag_ignore_all("-Woverride-init", "Allow field overrides in table");

#define PLATFORM(x)		\
	.platform = XE_##x,	\
	.platform_name = #x

#define NOP(x)	x

static const struct xe_graphics_desc graphics_xelp = {
	.hw_engine_mask = BIT(XE_HW_ENGINE_RCS0) | BIT(XE_HW_ENGINE_BCS0),

	.va_bits = 48,
	.vm_max_level = 3,
};

#define XE_HP_FEATURES \
	.has_range_tlb_invalidation = true, \
	.va_bits = 48, \
	.vm_max_level = 3

static const struct xe_graphics_desc graphics_xehpg = {
	.hw_engine_mask =
		BIT(XE_HW_ENGINE_RCS0) | BIT(XE_HW_ENGINE_BCS0) |
		BIT(XE_HW_ENGINE_CCS0) | BIT(XE_HW_ENGINE_CCS1) |
		BIT(XE_HW_ENGINE_CCS2) | BIT(XE_HW_ENGINE_CCS3),

	XE_HP_FEATURES,
	.vram_flags = XE_VRAM_FLAGS_NEED64K,

	.has_flat_ccs = 1,
};

static const struct xe_graphics_desc graphics_xehpc = {
	.hw_engine_mask =
		BIT(XE_HW_ENGINE_BCS0) | BIT(XE_HW_ENGINE_BCS1) |
		BIT(XE_HW_ENGINE_BCS2) | BIT(XE_HW_ENGINE_BCS3) |
		BIT(XE_HW_ENGINE_BCS4) | BIT(XE_HW_ENGINE_BCS5) |
		BIT(XE_HW_ENGINE_BCS6) | BIT(XE_HW_ENGINE_BCS7) |
		BIT(XE_HW_ENGINE_BCS8) |
		BIT(XE_HW_ENGINE_CCS0) | BIT(XE_HW_ENGINE_CCS1) |
		BIT(XE_HW_ENGINE_CCS2) | BIT(XE_HW_ENGINE_CCS3),

	XE_HP_FEATURES,
	.va_bits = 57,
	.vm_max_level = 4,
	.vram_flags = XE_VRAM_FLAGS_NEED64K,

	.has_asid = 1,
	.has_atomic_enable_pte_bit = 1,
	.has_usm = 1,
};

static const struct xe_graphics_desc graphics_xelpg = {
	.hw_engine_mask =
		BIT(XE_HW_ENGINE_RCS0) | BIT(XE_HW_ENGINE_BCS0) |
		BIT(XE_HW_ENGINE_CCS0),

	XE_HP_FEATURES,
};

#define XE2_GFX_FEATURES \
	.has_asid = 1, \
	.has_atomic_enable_pte_bit = 1, \
	.has_flat_ccs = 1, \
	.has_indirect_ring_state = 1, \
	.has_range_tlb_invalidation = 1, \
	.has_usm = 1, \
	.has_64bit_timestamp = 1, \
	.va_bits = 48, \
	.vm_max_level = 4, \
	.hw_engine_mask = \
		BIT(XE_HW_ENGINE_RCS0) | \
		BIT(XE_HW_ENGINE_BCS8) | BIT(XE_HW_ENGINE_BCS0) | \
		GENMASK(XE_HW_ENGINE_CCS3, XE_HW_ENGINE_CCS0)

static const struct xe_graphics_desc graphics_xe2 = {
	XE2_GFX_FEATURES,
};

static const struct xe_media_desc media_xem = {
	.hw_engine_mask =
		GENMASK(XE_HW_ENGINE_VCS7, XE_HW_ENGINE_VCS0) |
		GENMASK(XE_HW_ENGINE_VECS3, XE_HW_ENGINE_VECS0),
};

static const struct xe_media_desc media_xelpmp = {
	.hw_engine_mask =
		GENMASK(XE_HW_ENGINE_VCS7, XE_HW_ENGINE_VCS0) |
		GENMASK(XE_HW_ENGINE_VECS3, XE_HW_ENGINE_VECS0) |
		BIT(XE_HW_ENGINE_GSCCS0)
};

/* Pre-GMDID Graphics IPs */
static const struct xe_ip graphics_ip_xelp = { 1200, "Xe_LP", &graphics_xelp };
static const struct xe_ip graphics_ip_xelpp = { 1210, "Xe_LP+", &graphics_xelp };
static const struct xe_ip graphics_ip_xehpg = { 1255, "Xe_HPG", &graphics_xehpg };
static const struct xe_ip graphics_ip_xehpc = { 1260, "Xe_HPC", &graphics_xehpc };

/* GMDID-based Graphics IPs */
static const struct xe_ip graphics_ips[] = {
	{ 1270, "Xe_LPG", &graphics_xelpg },
	{ 1271, "Xe_LPG", &graphics_xelpg },
	{ 1274, "Xe_LPG+", &graphics_xelpg },
	{ 2001, "Xe2_HPG", &graphics_xe2 },
	{ 2004, "Xe2_LPG", &graphics_xe2 },
	{ 3000, "Xe3_LPG", &graphics_xe2 },
	{ 3001, "Xe3_LPG", &graphics_xe2 },
};

/* Pre-GMDID Media IPs */
static const struct xe_ip media_ip_xem = { 1200, "Xe_M", &media_xem };
static const struct xe_ip media_ip_xehpm = { 1255, "Xe_HPM", &media_xem };

/* GMDID-based Media IPs */
static const struct xe_ip media_ips[] = {
	{ 1300, "Xe_LPM+", &media_xelpmp },
	{ 1301, "Xe2_HPM", &media_xelpmp },
	{ 2000, "Xe2_LPM", &media_xelpmp },
	{ 3000, "Xe3_LPM", &media_xelpmp },
};

static const struct xe_device_desc tgl_desc = {
	.pre_gmdid_graphics_ip = &graphics_ip_xelp,
	.pre_gmdid_media_ip = &media_ip_xem,
	PLATFORM(TIGERLAKE),
	.dma_mask_size = 39,
	.has_display = true,
	.has_llc = true,
	.require_force_probe = true,
};

static const struct xe_device_desc rkl_desc = {
	.pre_gmdid_graphics_ip = &graphics_ip_xelp,
	.pre_gmdid_media_ip = &media_ip_xem,
	PLATFORM(ROCKETLAKE),
	.dma_mask_size = 39,
	.has_display = true,
	.has_llc = true,
	.require_force_probe = true,
};

static const u16 adls_rpls_ids[] = { INTEL_RPLS_IDS(NOP), 0 };

static const struct xe_device_desc adl_s_desc = {
	.pre_gmdid_graphics_ip = &graphics_ip_xelp,
	.pre_gmdid_media_ip = &media_ip_xem,
	PLATFORM(ALDERLAKE_S),
	.dma_mask_size = 39,
	.has_display = true,
	.has_llc = true,
	.require_force_probe = true,
	.subplatforms = (const struct xe_subplatform_desc[]) {
		{ XE_SUBPLATFORM_ALDERLAKE_S_RPLS, "RPLS", adls_rpls_ids },
		{},
	},
};

static const u16 adlp_rplu_ids[] = { INTEL_RPLU_IDS(NOP), 0 };

static const struct xe_device_desc adl_p_desc = {
	.pre_gmdid_graphics_ip = &graphics_ip_xelp,
	.pre_gmdid_media_ip = &media_ip_xem,
	PLATFORM(ALDERLAKE_P),
	.dma_mask_size = 39,
	.has_display = true,
	.has_llc = true,
	.require_force_probe = true,
	.subplatforms = (const struct xe_subplatform_desc[]) {
		{ XE_SUBPLATFORM_ALDERLAKE_P_RPLU, "RPLU", adlp_rplu_ids },
		{},
	},
};

static const struct xe_device_desc adl_n_desc = {
	.pre_gmdid_graphics_ip = &graphics_ip_xelp,
	.pre_gmdid_media_ip = &media_ip_xem,
	PLATFORM(ALDERLAKE_N),
	.dma_mask_size = 39,
	.has_display = true,
	.has_llc = true,
	.require_force_probe = true,
};

#define DGFX_FEATURES \
	.is_dgfx = 1

static const struct xe_device_desc dg1_desc = {
	.pre_gmdid_graphics_ip = &graphics_ip_xelpp,
	.pre_gmdid_media_ip = &media_ip_xem,
	DGFX_FEATURES,
	PLATFORM(DG1),
	.dma_mask_size = 39,
	.has_display = true,
	.has_heci_gscfi = 1,
	.require_force_probe = true,
};

static const u16 dg2_g10_ids[] = { INTEL_DG2_G10_IDS(NOP), INTEL_ATS_M150_IDS(NOP), 0 };
static const u16 dg2_g11_ids[] = { INTEL_DG2_G11_IDS(NOP), INTEL_ATS_M75_IDS(NOP), 0 };
static const u16 dg2_g12_ids[] = { INTEL_DG2_G12_IDS(NOP), 0 };

#define DG2_FEATURES \
	DGFX_FEATURES, \
	PLATFORM(DG2), \
	.has_heci_gscfi = 1, \
	.subplatforms = (const struct xe_subplatform_desc[]) { \
		{ XE_SUBPLATFORM_DG2_G10, "G10", dg2_g10_ids }, \
		{ XE_SUBPLATFORM_DG2_G11, "G11", dg2_g11_ids }, \
		{ XE_SUBPLATFORM_DG2_G12, "G12", dg2_g12_ids }, \
		{ } \
	}

static const struct xe_device_desc ats_m_desc = {
	.pre_gmdid_graphics_ip = &graphics_ip_xehpg,
	.pre_gmdid_media_ip = &media_ip_xehpm,
	.dma_mask_size = 46,
	.require_force_probe = true,

	DG2_FEATURES,
	.has_display = false,
};

static const struct xe_device_desc dg2_desc = {
	.pre_gmdid_graphics_ip = &graphics_ip_xehpg,
	.pre_gmdid_media_ip = &media_ip_xehpm,
	.dma_mask_size = 46,
	.require_force_probe = true,

	DG2_FEATURES,
	.has_display = true,
	.has_fan_control = true,
	.has_mbx_power_limits = false,
};

static const __maybe_unused struct xe_device_desc pvc_desc = {
	.pre_gmdid_graphics_ip = &graphics_ip_xehpc,
	DGFX_FEATURES,
	PLATFORM(PVC),
	.dma_mask_size = 52,
	.has_display = false,
	.has_heci_gscfi = 1,
	.max_remote_tiles = 1,
	.require_force_probe = true,
	.has_mbx_power_limits = false,
};

static const struct xe_device_desc mtl_desc = {
	/* .graphics and .media determined via GMD_ID */
	.require_force_probe = true,
	PLATFORM(METEORLAKE),
	.dma_mask_size = 46,
	.has_display = true,
	.has_pxp = true,
};

static const struct xe_device_desc lnl_desc = {
	PLATFORM(LUNARLAKE),
	.dma_mask_size = 46,
	.has_display = true,
	.has_pxp = true,
	.needs_scratch = true,
};

static const struct xe_device_desc bmg_desc = {
	DGFX_FEATURES,
	PLATFORM(BATTLEMAGE),
	.dma_mask_size = 46,
	.has_display = true,
	.has_fan_control = true,
	.has_mbx_power_limits = true,
	.has_heci_cscfi = 1,
	.needs_scratch = true,
};

static const struct xe_device_desc ptl_desc = {
	PLATFORM(PANTHERLAKE),
	.dma_mask_size = 46,
	.has_display = true,
	.has_sriov = true,
	.require_force_probe = true,
	.needs_scratch = true,
};

#undef PLATFORM
__diag_pop();

/*
 * Make sure any device matches here are from most specific to most
 * general.  For example, since the Quanta match is based on the subsystem
 * and subvendor IDs, we need it to come before the more general IVB
 * PCI ID matches, otherwise we'll use the wrong info struct above.
 */
static const struct pci_device_id pciidlist[] = {
	INTEL_TGL_IDS(INTEL_VGA_DEVICE, &tgl_desc),
	INTEL_RKL_IDS(INTEL_VGA_DEVICE, &rkl_desc),
	INTEL_ADLS_IDS(INTEL_VGA_DEVICE, &adl_s_desc),
	INTEL_ADLP_IDS(INTEL_VGA_DEVICE, &adl_p_desc),
	INTEL_ADLN_IDS(INTEL_VGA_DEVICE, &adl_n_desc),
	INTEL_RPLU_IDS(INTEL_VGA_DEVICE, &adl_p_desc),
	INTEL_RPLP_IDS(INTEL_VGA_DEVICE, &adl_p_desc),
	INTEL_RPLS_IDS(INTEL_VGA_DEVICE, &adl_s_desc),
	INTEL_DG1_IDS(INTEL_VGA_DEVICE, &dg1_desc),
	INTEL_ATS_M_IDS(INTEL_VGA_DEVICE, &ats_m_desc),
	INTEL_ARL_IDS(INTEL_VGA_DEVICE, &mtl_desc),
	INTEL_DG2_IDS(INTEL_VGA_DEVICE, &dg2_desc),
	INTEL_MTL_IDS(INTEL_VGA_DEVICE, &mtl_desc),
	INTEL_LNL_IDS(INTEL_VGA_DEVICE, &lnl_desc),
	INTEL_BMG_IDS(INTEL_VGA_DEVICE, &bmg_desc),
	INTEL_PTL_IDS(INTEL_VGA_DEVICE, &ptl_desc),
	{ }
};
MODULE_DEVICE_TABLE(pci, pciidlist);

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
	return device_id_in_list(device_id, xe_modparam.force_probe, false);
}

static bool id_blocked(u16 device_id)
{
	return device_id_in_list(device_id, xe_modparam.force_probe, true);
}

static const struct xe_subplatform_desc *
find_subplatform(const struct xe_device *xe, const struct xe_device_desc *desc)
{
	const struct xe_subplatform_desc *sp;
	const u16 *id;

	for (sp = desc->subplatforms; sp && sp->subplatform; sp++)
		for (id = sp->pciidlist; *id; id++)
			if (*id == xe->info.devid)
				return sp;

	return NULL;
}

enum xe_gmdid_type {
	GMDID_GRAPHICS,
	GMDID_MEDIA
};

static void read_gmdid(struct xe_device *xe, enum xe_gmdid_type type, u32 *ver, u32 *revid)
{
	struct xe_mmio *mmio = xe_root_tile_mmio(xe);
	struct xe_reg gmdid_reg = GMD_ID;
	u32 val;

	KUNIT_STATIC_STUB_REDIRECT(read_gmdid, xe, type, ver, revid);

	if (IS_SRIOV_VF(xe)) {
		struct xe_gt *gt = xe_root_mmio_gt(xe);

		/*
		 * To get the value of the GMDID register, VFs must obtain it
		 * from the GuC using MMIO communication.
		 *
		 * Note that at this point the xe_gt is not fully uninitialized
		 * and only basic access to MMIO registers is possible. To use
		 * our existing GuC communication functions we must perform at
		 * least basic xe_gt and xe_guc initialization.
		 *
		 * Since to obtain the value of GMDID_MEDIA we need to use the
		 * media GuC, temporarily tweak the gt type.
		 */
		xe_gt_assert(gt, gt->info.type == XE_GT_TYPE_UNINITIALIZED);

		if (type == GMDID_MEDIA) {
			gt->info.id = 1;
			gt->info.type = XE_GT_TYPE_MEDIA;
		} else {
			gt->info.id = 0;
			gt->info.type = XE_GT_TYPE_MAIN;
		}

		xe_gt_mmio_init(gt);
		xe_guc_comm_init_early(&gt->uc.guc);

		/* Don't bother with GMDID if failed to negotiate the GuC ABI */
		val = xe_gt_sriov_vf_bootstrap(gt) ? 0 : xe_gt_sriov_vf_gmdid(gt);

		/*
		 * Only undo xe_gt.info here, the remaining changes made above
		 * will be overwritten as part of the regular initialization.
		 */
		gt->info.id = 0;
		gt->info.type = XE_GT_TYPE_UNINITIALIZED;
	} else {
		/*
		 * GMD_ID is a GT register, but at this point in the driver
		 * init we haven't fully initialized the GT yet so we need to
		 * read the register with the tile's MMIO accessor.  That means
		 * we need to apply the GSI offset manually since it won't get
		 * automatically added as it would if we were using a GT mmio
		 * accessor.
		 */
		if (type == GMDID_MEDIA)
			gmdid_reg.addr += MEDIA_GT_GSI_OFFSET;

		val = xe_mmio_read32(mmio, gmdid_reg);
	}

	*ver = REG_FIELD_GET(GMD_ID_ARCH_MASK, val) * 100 + REG_FIELD_GET(GMD_ID_RELEASE_MASK, val);
	*revid = REG_FIELD_GET(GMD_ID_REVID, val);
}

/*
 * Read IP version from hardware and select graphics/media IP descriptors
 * based on the result.
 */
static void handle_gmdid(struct xe_device *xe,
			 const struct xe_ip **graphics_ip,
			 const struct xe_ip **media_ip,
			 u32 *graphics_revid,
			 u32 *media_revid)
{
	u32 ver;

	*graphics_ip = NULL;
	*media_ip = NULL;

	read_gmdid(xe, GMDID_GRAPHICS, &ver, graphics_revid);

	for (int i = 0; i < ARRAY_SIZE(graphics_ips); i++) {
		if (ver == graphics_ips[i].verx100) {
			*graphics_ip = &graphics_ips[i];

			break;
		}
	}

	if (!*graphics_ip) {
		drm_err(&xe->drm, "Hardware reports unknown graphics version %u.%02u\n",
			ver / 100, ver % 100);
	}

	read_gmdid(xe, GMDID_MEDIA, &ver, media_revid);
	/* Media may legitimately be fused off / not present */
	if (ver == 0)
		return;

	for (int i = 0; i < ARRAY_SIZE(media_ips); i++) {
		if (ver == media_ips[i].verx100) {
			*media_ip = &media_ips[i];

			break;
		}
	}

	if (!*media_ip) {
		drm_err(&xe->drm, "Hardware reports unknown media version %u.%02u\n",
			ver / 100, ver % 100);
	}
}

/*
 * Initialize device info content that only depends on static driver_data
 * passed to the driver at probe time from PCI ID table.
 */
static int xe_info_init_early(struct xe_device *xe,
			      const struct xe_device_desc *desc,
			      const struct xe_subplatform_desc *subplatform_desc)
{
	int err;

	xe->info.platform_name = desc->platform_name;
	xe->info.platform = desc->platform;
	xe->info.subplatform = subplatform_desc ?
		subplatform_desc->subplatform : XE_SUBPLATFORM_NONE;

	xe->info.dma_mask_size = desc->dma_mask_size;
	xe->info.is_dgfx = desc->is_dgfx;
	xe->info.has_fan_control = desc->has_fan_control;
	xe->info.has_mbx_power_limits = desc->has_mbx_power_limits;
	xe->info.has_heci_gscfi = desc->has_heci_gscfi;
	xe->info.has_heci_cscfi = desc->has_heci_cscfi;
	xe->info.has_llc = desc->has_llc;
	xe->info.has_pxp = desc->has_pxp;
	xe->info.has_sriov = desc->has_sriov;
	xe->info.skip_guc_pc = desc->skip_guc_pc;
	xe->info.skip_mtcfg = desc->skip_mtcfg;
	xe->info.skip_pcode = desc->skip_pcode;
	xe->info.needs_scratch = desc->needs_scratch;

	xe->info.probe_display = IS_ENABLED(CONFIG_DRM_XE_DISPLAY) &&
				 xe_modparam.probe_display &&
				 desc->has_display;
	xe->info.tile_count = 1 + desc->max_remote_tiles;

	err = xe_tile_init_early(xe_device_get_root_tile(xe), xe, 0);
	if (err)
		return err;

	return 0;
}

/*
 * Initialize device info content that does require knowledge about
 * graphics / media IP version.
 * Make sure that GT / tile structures allocated by the driver match the data
 * present in device info.
 */
static int xe_info_init(struct xe_device *xe,
			const struct xe_device_desc *desc)
{
	u32 graphics_gmdid_revid = 0, media_gmdid_revid = 0;
	const struct xe_ip *graphics_ip;
	const struct xe_ip *media_ip;
	const struct xe_graphics_desc *graphics_desc;
	const struct xe_media_desc *media_desc;
	struct xe_tile *tile;
	struct xe_gt *gt;
	u8 id;

	/*
	 * If this platform supports GMD_ID, we'll detect the proper IP
	 * descriptor to use from hardware registers.
	 * desc->pre_gmdid_graphics_ip will only ever be set at this point for
	 * platforms before GMD_ID. In that case the IP descriptions and
	 * versions are simply derived from that.
	 */
	if (desc->pre_gmdid_graphics_ip) {
		graphics_ip = desc->pre_gmdid_graphics_ip;
		media_ip = desc->pre_gmdid_media_ip;
		xe->info.step = xe_step_pre_gmdid_get(xe);
	} else {
		xe_assert(xe, !desc->pre_gmdid_media_ip);
		handle_gmdid(xe, &graphics_ip, &media_ip,
			     &graphics_gmdid_revid, &media_gmdid_revid);
		xe->info.step = xe_step_gmdid_get(xe,
						  graphics_gmdid_revid,
						  media_gmdid_revid);
	}

	/*
	 * If we couldn't detect the graphics IP, that's considered a fatal
	 * error and we should abort driver load.  Failing to detect media
	 * IP is non-fatal; we'll just proceed without enabling media support.
	 */
	if (!graphics_ip)
		return -ENODEV;

	xe->info.graphics_verx100 = graphics_ip->verx100;
	xe->info.graphics_name = graphics_ip->name;
	graphics_desc = graphics_ip->desc;

	if (media_ip) {
		xe->info.media_verx100 = media_ip->verx100;
		xe->info.media_name = media_ip->name;
		media_desc = media_ip->desc;
	} else {
		xe->info.media_name = "none";
		media_desc = NULL;
	}

	xe->info.vram_flags = graphics_desc->vram_flags;
	xe->info.va_bits = graphics_desc->va_bits;
	xe->info.vm_max_level = graphics_desc->vm_max_level;
	xe->info.has_asid = graphics_desc->has_asid;
	xe->info.has_atomic_enable_pte_bit = graphics_desc->has_atomic_enable_pte_bit;
	if (xe->info.platform != XE_PVC)
		xe->info.has_device_atomics_on_smem = 1;

	/* Runtime detection may change this later */
	xe->info.has_flat_ccs = graphics_desc->has_flat_ccs;

	xe->info.has_range_tlb_invalidation = graphics_desc->has_range_tlb_invalidation;
	xe->info.has_usm = graphics_desc->has_usm;
	xe->info.has_64bit_timestamp = graphics_desc->has_64bit_timestamp;

	for_each_remote_tile(tile, xe, id) {
		int err;

		err = xe_tile_init_early(tile, xe, id);
		if (err)
			return err;
	}

	/*
	 * All platforms have at least one primary GT.  Any platform with media
	 * version 13 or higher has an additional dedicated media GT.  And
	 * depending on the graphics IP there may be additional "remote tiles."
	 * All of these together determine the overall GT count.
	 */
	for_each_tile(tile, xe, id) {
		gt = tile->primary_gt;
		gt->info.id = xe->info.gt_count++;
		gt->info.type = XE_GT_TYPE_MAIN;
		gt->info.has_indirect_ring_state = graphics_desc->has_indirect_ring_state;
		gt->info.engine_mask = graphics_desc->hw_engine_mask;

		if (MEDIA_VER(xe) < 13 && media_desc)
			gt->info.engine_mask |= media_desc->hw_engine_mask;

		if (MEDIA_VER(xe) < 13 || !media_desc)
			continue;

		/*
		 * Allocate and setup media GT for platforms with standalone
		 * media.
		 */
		tile->media_gt = xe_gt_alloc(tile);
		if (IS_ERR(tile->media_gt))
			return PTR_ERR(tile->media_gt);

		gt = tile->media_gt;
		gt->info.type = XE_GT_TYPE_MEDIA;
		gt->info.has_indirect_ring_state = media_desc->has_indirect_ring_state;
		gt->info.engine_mask = media_desc->hw_engine_mask;

		/*
		 * FIXME: At the moment multi-tile and standalone media are
		 * mutually exclusive on current platforms.  We'll need to
		 * come up with a better way to number GTs if we ever wind
		 * up with platforms that support both together.
		 */
		drm_WARN_ON(&xe->drm, id != 0);
		gt->info.id = xe->info.gt_count++;
	}

	return 0;
}

static void xe_pci_remove(struct pci_dev *pdev)
{
	struct xe_device *xe = pdev_to_xe_device(pdev);

	if (IS_SRIOV_PF(xe))
		xe_pci_sriov_configure(pdev, 0);

	if (xe_survivability_mode_is_enabled(xe))
		return;

	xe_device_remove(xe);
	xe_pm_fini(xe);
}

/*
 * Probe the PCI device, initialize various parts of the driver.
 *
 * Fault injection is used to test the error paths of some initialization
 * functions called either directly from xe_pci_probe() or indirectly for
 * example through xe_device_probe(). Those functions use the kernel fault
 * injection capabilities infrastructure, see
 * Documentation/fault-injection/fault-injection.rst for details. The macro
 * ALLOW_ERROR_INJECTION() is used to conditionally skip function execution
 * at runtime and use a provided return value. The first requirement for
 * error injectable functions is proper handling of the error code by the
 * caller for recovery, which is always the case here. The second
 * requirement is that no state is changed before the first error return.
 * It is not strictly fulfilled for all initialization functions using the
 * ALLOW_ERROR_INJECTION() macro but this is acceptable because for those
 * error cases at probe time, the error code is simply propagated up by the
 * caller. Therefore there is no consequence on those specific callers when
 * function error injection skips the whole function.
 */
static int xe_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct xe_device_desc *desc = (const void *)ent->driver_data;
	const struct xe_subplatform_desc *subplatform_desc;
	struct xe_device *xe;
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

	if (xe_display_driver_probe_defer(pdev))
		return -EPROBE_DEFER;

	err = pcim_enable_device(pdev);
	if (err)
		return err;

	xe = xe_device_create(pdev, ent);
	if (IS_ERR(xe))
		return PTR_ERR(xe);

	pci_set_drvdata(pdev, &xe->drm);

	xe_pm_assert_unbounded_bridge(xe);
	subplatform_desc = find_subplatform(xe, desc);

	pci_set_master(pdev);

	err = xe_info_init_early(xe, desc, subplatform_desc);
	if (err)
		return err;

	err = xe_device_probe_early(xe);
	/*
	 * In Boot Survivability mode, no drm card is exposed and driver
	 * is loaded with bare minimum to allow for firmware to be
	 * flashed through mei. Return success, if survivability mode
	 * is enabled due to pcode failure or configfs being set
	 */
	if (xe_survivability_mode_is_enabled(xe))
		return 0;

	if (err)
		return err;

	err = xe_info_init(xe, desc);
	if (err)
		return err;

	err = xe_display_probe(xe);
	if (err)
		return err;

	drm_dbg(&xe->drm, "%s %s %04x:%04x dgfx:%d gfx:%s (%d.%02d) media:%s (%d.%02d) display:%s dma_m_s:%d tc:%d gscfi:%d cscfi:%d",
		desc->platform_name,
		subplatform_desc ? subplatform_desc->name : "",
		xe->info.devid, xe->info.revid,
		xe->info.is_dgfx,
		xe->info.graphics_name,
		xe->info.graphics_verx100 / 100,
		xe->info.graphics_verx100 % 100,
		xe->info.media_name,
		xe->info.media_verx100 / 100,
		xe->info.media_verx100 % 100,
		str_yes_no(xe->info.probe_display),
		xe->info.dma_mask_size, xe->info.tile_count,
		xe->info.has_heci_gscfi, xe->info.has_heci_cscfi);

	drm_dbg(&xe->drm, "Stepping = (G:%s, M:%s, B:%s)\n",
		xe_step_name(xe->info.step.graphics),
		xe_step_name(xe->info.step.media),
		xe_step_name(xe->info.step.basedie));

	drm_dbg(&xe->drm, "SR-IOV support: %s (mode: %s)\n",
		str_yes_no(xe_device_has_sriov(xe)),
		xe_sriov_mode_to_string(xe_device_sriov_mode(xe)));

	err = xe_pm_init_early(xe);
	if (err)
		return err;

	err = xe_device_probe(xe);
	if (err)
		return err;

	err = xe_pm_init(xe);
	if (err)
		goto err_driver_cleanup;

	drm_dbg(&xe->drm, "d3cold: capable=%s\n",
		str_yes_no(xe->d3cold.capable));

	return 0;

err_driver_cleanup:
	xe_pci_remove(pdev);
	return err;
}

static void xe_pci_shutdown(struct pci_dev *pdev)
{
	xe_device_shutdown(pdev_to_xe_device(pdev));
}

#ifdef CONFIG_PM_SLEEP
static void d3cold_toggle(struct pci_dev *pdev, enum toggle_d3cold toggle)
{
	struct xe_device *xe = pdev_to_xe_device(pdev);
	struct pci_dev *root_pdev;

	if (!xe->d3cold.capable)
		return;

	root_pdev = pcie_find_root_port(pdev);
	if (!root_pdev)
		return;

	switch (toggle) {
	case D3COLD_DISABLE:
		pci_d3cold_disable(root_pdev);
		break;
	case D3COLD_ENABLE:
		pci_d3cold_enable(root_pdev);
		break;
	}
}

static int xe_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	int err;

	if (xe_survivability_mode_is_enabled(xe))
		return -EBUSY;

	err = xe_pm_suspend(xe);
	if (err)
		return err;

	/*
	 * Enabling D3Cold is needed for S2Idle/S0ix.
	 * It is save to allow here since xe_pm_suspend has evicted
	 * the local memory and the direct complete optimization is disabled.
	 */
	d3cold_toggle(pdev, D3COLD_ENABLE);

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3cold);

	return 0;
}

static int xe_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int err;

	/* Give back the D3Cold decision to the runtime P M*/
	d3cold_toggle(pdev, D3COLD_DISABLE);

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

static int xe_pci_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	int err;

	err = xe_pm_runtime_suspend(xe);
	if (err)
		return err;

	pci_save_state(pdev);

	if (xe->d3cold.allowed) {
		d3cold_toggle(pdev, D3COLD_ENABLE);
		pci_disable_device(pdev);
		pci_ignore_hotplug(pdev);
		pci_set_power_state(pdev, PCI_D3cold);
	} else {
		d3cold_toggle(pdev, D3COLD_DISABLE);
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

	if (xe->d3cold.allowed) {
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

	xe_pm_d3cold_allowed_toggle(xe);

	return 0;
}

static const struct dev_pm_ops xe_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xe_pci_suspend, xe_pci_resume)
	SET_RUNTIME_PM_OPS(xe_pci_runtime_suspend, xe_pci_runtime_resume, xe_pci_runtime_idle)
};
#endif

static struct pci_driver xe_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = xe_pci_probe,
	.remove = xe_pci_remove,
	.shutdown = xe_pci_shutdown,
	.sriov_configure = xe_pci_sriov_configure,
#ifdef CONFIG_PM_SLEEP
	.driver.pm = &xe_pm_ops,
#endif
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
#include "tests/xe_pci.c"
#endif

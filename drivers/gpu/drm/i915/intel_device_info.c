/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/string_helpers.h>

#include <drm/drm_print.h>
#include <drm/i915_pciids.h>

#include "gt/intel_gt_regs.h"
#include "i915_drv.h"
#include "i915_reg.h"
#include "i915_utils.h"
#include "intel_device_info.h"

#define PLATFORM_NAME(x) [INTEL_##x] = #x
static const char * const platform_names[] = {
	PLATFORM_NAME(I830),
	PLATFORM_NAME(I845G),
	PLATFORM_NAME(I85X),
	PLATFORM_NAME(I865G),
	PLATFORM_NAME(I915G),
	PLATFORM_NAME(I915GM),
	PLATFORM_NAME(I945G),
	PLATFORM_NAME(I945GM),
	PLATFORM_NAME(G33),
	PLATFORM_NAME(PINEVIEW),
	PLATFORM_NAME(I965G),
	PLATFORM_NAME(I965GM),
	PLATFORM_NAME(G45),
	PLATFORM_NAME(GM45),
	PLATFORM_NAME(IRONLAKE),
	PLATFORM_NAME(SANDYBRIDGE),
	PLATFORM_NAME(IVYBRIDGE),
	PLATFORM_NAME(VALLEYVIEW),
	PLATFORM_NAME(HASWELL),
	PLATFORM_NAME(BROADWELL),
	PLATFORM_NAME(CHERRYVIEW),
	PLATFORM_NAME(SKYLAKE),
	PLATFORM_NAME(BROXTON),
	PLATFORM_NAME(KABYLAKE),
	PLATFORM_NAME(GEMINILAKE),
	PLATFORM_NAME(COFFEELAKE),
	PLATFORM_NAME(COMETLAKE),
	PLATFORM_NAME(ICELAKE),
	PLATFORM_NAME(ELKHARTLAKE),
	PLATFORM_NAME(JASPERLAKE),
	PLATFORM_NAME(TIGERLAKE),
	PLATFORM_NAME(ROCKETLAKE),
	PLATFORM_NAME(DG1),
	PLATFORM_NAME(ALDERLAKE_S),
	PLATFORM_NAME(ALDERLAKE_P),
	PLATFORM_NAME(XEHPSDV),
	PLATFORM_NAME(DG2),
	PLATFORM_NAME(PONTEVECCHIO),
	PLATFORM_NAME(METEORLAKE),
};
#undef PLATFORM_NAME

const char *intel_platform_name(enum intel_platform platform)
{
	BUILD_BUG_ON(ARRAY_SIZE(platform_names) != INTEL_MAX_PLATFORMS);

	if (WARN_ON_ONCE(platform >= ARRAY_SIZE(platform_names) ||
			 platform_names[platform] == NULL))
		return "<unknown>";

	return platform_names[platform];
}

void intel_device_info_print(const struct intel_device_info *info,
			     const struct intel_runtime_info *runtime,
			     struct drm_printer *p)
{
	if (runtime->graphics.ip.rel)
		drm_printf(p, "graphics version: %u.%02u\n",
			   runtime->graphics.ip.ver,
			   runtime->graphics.ip.rel);
	else
		drm_printf(p, "graphics version: %u\n",
			   runtime->graphics.ip.ver);

	if (runtime->media.ip.rel)
		drm_printf(p, "media version: %u.%02u\n",
			   runtime->media.ip.ver,
			   runtime->media.ip.rel);
	else
		drm_printf(p, "media version: %u\n",
			   runtime->media.ip.ver);

	drm_printf(p, "graphics stepping: %s\n", intel_step_name(runtime->step.graphics_step));
	drm_printf(p, "media stepping: %s\n", intel_step_name(runtime->step.media_step));
	drm_printf(p, "display stepping: %s\n", intel_step_name(runtime->step.display_step));
	drm_printf(p, "base die stepping: %s\n", intel_step_name(runtime->step.basedie_step));

	drm_printf(p, "gt: %d\n", info->gt);
	drm_printf(p, "memory-regions: 0x%x\n", info->memory_regions);
	drm_printf(p, "page-sizes: 0x%x\n", runtime->page_sizes);
	drm_printf(p, "platform: %s\n", intel_platform_name(info->platform));
	drm_printf(p, "ppgtt-size: %d\n", runtime->ppgtt_size);
	drm_printf(p, "ppgtt-type: %d\n", runtime->ppgtt_type);
	drm_printf(p, "dma_mask_size: %u\n", info->dma_mask_size);

#define PRINT_FLAG(name) drm_printf(p, "%s: %s\n", #name, str_yes_no(info->name))
	DEV_INFO_FOR_EACH_FLAG(PRINT_FLAG);
#undef PRINT_FLAG

	drm_printf(p, "has_pooled_eu: %s\n", str_yes_no(runtime->has_pooled_eu));
	drm_printf(p, "rawclk rate: %u kHz\n", runtime->rawclk_freq);
}

#undef INTEL_VGA_DEVICE
#define INTEL_VGA_DEVICE(id, info) (id)

static const u16 subplatform_ult_ids[] = {
	INTEL_HSW_ULT_GT1_IDS(0),
	INTEL_HSW_ULT_GT2_IDS(0),
	INTEL_HSW_ULT_GT3_IDS(0),
	INTEL_BDW_ULT_GT1_IDS(0),
	INTEL_BDW_ULT_GT2_IDS(0),
	INTEL_BDW_ULT_GT3_IDS(0),
	INTEL_BDW_ULT_RSVD_IDS(0),
	INTEL_SKL_ULT_GT1_IDS(0),
	INTEL_SKL_ULT_GT2_IDS(0),
	INTEL_SKL_ULT_GT3_IDS(0),
	INTEL_KBL_ULT_GT1_IDS(0),
	INTEL_KBL_ULT_GT2_IDS(0),
	INTEL_KBL_ULT_GT3_IDS(0),
	INTEL_CFL_U_GT2_IDS(0),
	INTEL_CFL_U_GT3_IDS(0),
	INTEL_WHL_U_GT1_IDS(0),
	INTEL_WHL_U_GT2_IDS(0),
	INTEL_WHL_U_GT3_IDS(0),
	INTEL_CML_U_GT1_IDS(0),
	INTEL_CML_U_GT2_IDS(0),
};

static const u16 subplatform_ulx_ids[] = {
	INTEL_HSW_ULX_GT1_IDS(0),
	INTEL_HSW_ULX_GT2_IDS(0),
	INTEL_BDW_ULX_GT1_IDS(0),
	INTEL_BDW_ULX_GT2_IDS(0),
	INTEL_BDW_ULX_GT3_IDS(0),
	INTEL_BDW_ULX_RSVD_IDS(0),
	INTEL_SKL_ULX_GT1_IDS(0),
	INTEL_SKL_ULX_GT2_IDS(0),
	INTEL_KBL_ULX_GT1_IDS(0),
	INTEL_KBL_ULX_GT2_IDS(0),
	INTEL_AML_KBL_GT2_IDS(0),
	INTEL_AML_CFL_GT2_IDS(0),
};

static const u16 subplatform_portf_ids[] = {
	INTEL_ICL_PORT_F_IDS(0),
};

static const u16 subplatform_uy_ids[] = {
	INTEL_TGL_12_GT2_IDS(0),
};

static const u16 subplatform_n_ids[] = {
	INTEL_ADLN_IDS(0),
};

static const u16 subplatform_rpl_ids[] = {
	INTEL_RPLS_IDS(0),
	INTEL_RPLP_IDS(0),
};

static const u16 subplatform_rplu_ids[] = {
	INTEL_RPLU_IDS(0),
};

static const u16 subplatform_g10_ids[] = {
	INTEL_DG2_G10_IDS(0),
	INTEL_ATS_M150_IDS(0),
};

static const u16 subplatform_g11_ids[] = {
	INTEL_DG2_G11_IDS(0),
	INTEL_ATS_M75_IDS(0),
};

static const u16 subplatform_g12_ids[] = {
	INTEL_DG2_G12_IDS(0),
};

static bool find_devid(u16 id, const u16 *p, unsigned int num)
{
	for (; num; num--, p++) {
		if (*p == id)
			return true;
	}

	return false;
}

static void intel_device_info_subplatform_init(struct drm_i915_private *i915)
{
	const struct intel_device_info *info = INTEL_INFO(i915);
	const struct intel_runtime_info *rinfo = RUNTIME_INFO(i915);
	const unsigned int pi = __platform_mask_index(rinfo, info->platform);
	const unsigned int pb = __platform_mask_bit(rinfo, info->platform);
	u16 devid = INTEL_DEVID(i915);
	u32 mask = 0;

	/* Make sure IS_<platform> checks are working. */
	RUNTIME_INFO(i915)->platform_mask[pi] = BIT(pb);

	/* Find and mark subplatform bits based on the PCI device id. */
	if (find_devid(devid, subplatform_ult_ids,
		       ARRAY_SIZE(subplatform_ult_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_ULT);
	} else if (find_devid(devid, subplatform_ulx_ids,
			      ARRAY_SIZE(subplatform_ulx_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_ULX);
		if (IS_HASWELL(i915) || IS_BROADWELL(i915)) {
			/* ULX machines are also considered ULT. */
			mask |= BIT(INTEL_SUBPLATFORM_ULT);
		}
	} else if (find_devid(devid, subplatform_portf_ids,
			      ARRAY_SIZE(subplatform_portf_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_PORTF);
	} else if (find_devid(devid, subplatform_uy_ids,
			   ARRAY_SIZE(subplatform_uy_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_UY);
	} else if (find_devid(devid, subplatform_n_ids,
				ARRAY_SIZE(subplatform_n_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_N);
	} else if (find_devid(devid, subplatform_rpl_ids,
			      ARRAY_SIZE(subplatform_rpl_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_RPL);
		if (find_devid(devid, subplatform_rplu_ids,
			       ARRAY_SIZE(subplatform_rplu_ids)))
			mask |= BIT(INTEL_SUBPLATFORM_RPLU);
	} else if (find_devid(devid, subplatform_g10_ids,
			      ARRAY_SIZE(subplatform_g10_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_G10);
	} else if (find_devid(devid, subplatform_g11_ids,
			      ARRAY_SIZE(subplatform_g11_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_G11);
	} else if (find_devid(devid, subplatform_g12_ids,
			      ARRAY_SIZE(subplatform_g12_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_G12);
	}

	GEM_BUG_ON(mask & ~INTEL_SUBPLATFORM_MASK);

	RUNTIME_INFO(i915)->platform_mask[pi] |= mask;
}

static void ip_ver_read(struct drm_i915_private *i915, u32 offset, struct intel_ip_version *ip)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	void __iomem *addr;
	u32 val;
	u8 expected_ver = ip->ver;
	u8 expected_rel = ip->rel;

	addr = pci_iomap_range(pdev, 0, offset, sizeof(u32));
	if (drm_WARN_ON(&i915->drm, !addr))
		return;

	val = ioread32(addr);
	pci_iounmap(pdev, addr);

	ip->ver = REG_FIELD_GET(GMD_ID_ARCH_MASK, val);
	ip->rel = REG_FIELD_GET(GMD_ID_RELEASE_MASK, val);
	ip->step = REG_FIELD_GET(GMD_ID_STEP, val);

	/* Sanity check against expected versions from device info */
	if (IP_VER(ip->ver, ip->rel) < IP_VER(expected_ver, expected_rel))
		drm_dbg(&i915->drm,
			"Hardware reports GMD IP version %u.%u (REG[0x%x] = 0x%08x) but minimum expected is %u.%u\n",
			ip->ver, ip->rel, offset, val, expected_ver, expected_rel);
}

/*
 * Setup the graphics version for the current device.  This must be done before
 * any code that performs checks on GRAPHICS_VER or DISPLAY_VER, so this
 * function should be called very early in the driver initialization sequence.
 *
 * Regular MMIO access is not yet setup at the point this function is called so
 * we peek at the appropriate MMIO offset directly.  The GMD_ID register is
 * part of an 'always on' power well by design, so we don't need to worry about
 * forcewake while reading it.
 */
static void intel_ipver_early_init(struct drm_i915_private *i915)
{
	struct intel_runtime_info *runtime = RUNTIME_INFO(i915);

	if (!HAS_GMD_ID(i915)) {
		drm_WARN_ON(&i915->drm, RUNTIME_INFO(i915)->graphics.ip.ver > 12);
		/*
		 * On older platforms, graphics and media share the same ip
		 * version and release.
		 */
		RUNTIME_INFO(i915)->media.ip =
			RUNTIME_INFO(i915)->graphics.ip;
		return;
	}

	ip_ver_read(i915, i915_mmio_reg_offset(GMD_ID_GRAPHICS),
		    &runtime->graphics.ip);
	/* Wa_22012778468 */
	if (runtime->graphics.ip.ver == 0x0 &&
	    INTEL_INFO(i915)->platform == INTEL_METEORLAKE) {
		RUNTIME_INFO(i915)->graphics.ip.ver = 12;
		RUNTIME_INFO(i915)->graphics.ip.rel = 70;
	}
	ip_ver_read(i915, i915_mmio_reg_offset(GMD_ID_MEDIA),
		    &runtime->media.ip);
}

/**
 * intel_device_info_runtime_init_early - initialize early runtime info
 * @i915: the i915 device
 *
 * Determine early intel_device_info fields at runtime. This function needs
 * to be called before the MMIO has been setup.
 */
void intel_device_info_runtime_init_early(struct drm_i915_private *i915)
{
	intel_ipver_early_init(i915);
	intel_device_info_subplatform_init(i915);
}

/**
 * intel_device_info_runtime_init - initialize runtime info
 * @dev_priv: the i915 device
 *
 * Determine various intel_device_info fields at runtime.
 *
 * Use it when either:
 *   - it's judged too laborious to fill n static structures with the limit
 *     when a simple if statement does the job,
 *   - run-time checks (eg read fuse/strap registers) are needed.
 *
 * This function needs to be called:
 *   - after the MMIO has been setup as we are reading registers,
 *   - after the PCH has been detected,
 *   - before the first usage of the fields it can tweak.
 */
void intel_device_info_runtime_init(struct drm_i915_private *dev_priv)
{
	struct intel_runtime_info *runtime = RUNTIME_INFO(dev_priv);

	BUILD_BUG_ON(BITS_PER_TYPE(intel_engine_mask_t) < I915_NUM_ENGINES);

	if (GRAPHICS_VER(dev_priv) == 6 && i915_vtd_active(dev_priv)) {
		drm_info(&dev_priv->drm,
			 "Disabling ppGTT for VT-d support\n");
		runtime->ppgtt_type = INTEL_PPGTT_NONE;
	}

	runtime->rawclk_freq = intel_read_rawclk(dev_priv);
	drm_dbg(&dev_priv->drm, "rawclk rate: %d kHz\n", runtime->rawclk_freq);

}

/*
 * Set up device info and initial runtime info at driver create.
 *
 * Note: i915 is only an allocated blob of memory at this point.
 */
void intel_device_info_driver_create(struct drm_i915_private *i915,
				     u16 device_id,
				     const struct intel_device_info *match_info)
{
	struct intel_runtime_info *runtime;

	/* Setup INTEL_INFO() */
	i915->__info = match_info;

	/* Initialize initial runtime info from static const data and pdev. */
	runtime = RUNTIME_INFO(i915);
	memcpy(runtime, &INTEL_INFO(i915)->__runtime, sizeof(*runtime));

	runtime->device_id = device_id;
}

void intel_driver_caps_print(const struct intel_driver_caps *caps,
			     struct drm_printer *p)
{
	drm_printf(p, "Has logical contexts? %s\n",
		   str_yes_no(caps->has_logical_contexts));
	drm_printf(p, "scheduler: 0x%x\n", caps->scheduler);
}

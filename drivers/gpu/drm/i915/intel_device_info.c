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

#include <drm/drm_print.h>
#include <drm/i915_pciids.h>

#include "display/intel_cdclk.h"
#include "display/intel_de.h"
#include "intel_device_info.h"
#include "i915_drv.h"

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

void intel_device_info_print_static(const struct intel_device_info *info,
				    struct drm_printer *p)
{
	if (info->graphics.rel)
		drm_printf(p, "graphics version: %u.%02u\n", info->graphics.ver,
			   info->graphics.rel);
	else
		drm_printf(p, "graphics version: %u\n", info->graphics.ver);

	if (info->media.rel)
		drm_printf(p, "media version: %u.%02u\n", info->media.ver, info->media.rel);
	else
		drm_printf(p, "media version: %u\n", info->media.ver);

	if (info->display.rel)
		drm_printf(p, "display version: %u.%02u\n", info->display.ver, info->display.rel);
	else
		drm_printf(p, "display version: %u\n", info->display.ver);

	drm_printf(p, "gt: %d\n", info->gt);
	drm_printf(p, "memory-regions: %x\n", info->memory_regions);
	drm_printf(p, "page-sizes: %x\n", info->page_sizes);
	drm_printf(p, "platform: %s\n", intel_platform_name(info->platform));
	drm_printf(p, "ppgtt-size: %d\n", info->ppgtt_size);
	drm_printf(p, "ppgtt-type: %d\n", info->ppgtt_type);
	drm_printf(p, "dma_mask_size: %u\n", info->dma_mask_size);

#define PRINT_FLAG(name) drm_printf(p, "%s: %s\n", #name, yesno(info->name))
	DEV_INFO_FOR_EACH_FLAG(PRINT_FLAG);
#undef PRINT_FLAG

#define PRINT_FLAG(name) drm_printf(p, "%s: %s\n", #name, yesno(info->display.name));
	DEV_INFO_DISPLAY_FOR_EACH_FLAG(PRINT_FLAG);
#undef PRINT_FLAG
}

void intel_device_info_print_runtime(const struct intel_runtime_info *info,
				     struct drm_printer *p)
{
	drm_printf(p, "rawclk rate: %u kHz\n", info->rawclk_freq);
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

static const u16 subplatform_rpls_ids[] = {
	INTEL_RPLS_IDS(0),
};

static bool find_devid(u16 id, const u16 *p, unsigned int num)
{
	for (; num; num--, p++) {
		if (*p == id)
			return true;
	}

	return false;
}

void intel_device_info_subplatform_init(struct drm_i915_private *i915)
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
	} else if (find_devid(devid, subplatform_rpls_ids,
			      ARRAY_SIZE(subplatform_rpls_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_RPL_S);
	}

	if (IS_TIGERLAKE(i915)) {
		struct pci_dev *root, *pdev = to_pci_dev(i915->drm.dev);

		root = list_first_entry(&pdev->bus->devices, typeof(*root), bus_list);

		drm_WARN_ON(&i915->drm, mask);
		drm_WARN_ON(&i915->drm, (root->device & TGL_ROOT_DEVICE_MASK) !=
			    TGL_ROOT_DEVICE_ID);

		switch (root->device & TGL_ROOT_DEVICE_SKU_MASK) {
		case TGL_ROOT_DEVICE_SKU_ULX:
			mask = BIT(INTEL_SUBPLATFORM_ULX);
			break;
		case TGL_ROOT_DEVICE_SKU_ULT:
			mask = BIT(INTEL_SUBPLATFORM_ULT);
			break;
		}
	}

	GEM_BUG_ON(mask & ~INTEL_SUBPLATFORM_MASK);

	RUNTIME_INFO(i915)->platform_mask[pi] |= mask;
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
	struct intel_device_info *info = mkwrite_device_info(dev_priv);
	struct intel_runtime_info *runtime = RUNTIME_INFO(dev_priv);
	enum pipe pipe;

	/* Wa_14011765242: adl-s A0,A1 */
	if (IS_ADLS_DISPLAY_STEP(dev_priv, STEP_A0, STEP_A2))
		for_each_pipe(dev_priv, pipe)
			runtime->num_scalers[pipe] = 0;
	else if (DISPLAY_VER(dev_priv) >= 11) {
		for_each_pipe(dev_priv, pipe)
			runtime->num_scalers[pipe] = 2;
	} else if (DISPLAY_VER(dev_priv) >= 9) {
		runtime->num_scalers[PIPE_A] = 2;
		runtime->num_scalers[PIPE_B] = 2;
		runtime->num_scalers[PIPE_C] = 1;
	}

	BUILD_BUG_ON(BITS_PER_TYPE(intel_engine_mask_t) < I915_NUM_ENGINES);

	if (DISPLAY_VER(dev_priv) >= 13 || HAS_D12_PLANE_MINIMIZATION(dev_priv))
		for_each_pipe(dev_priv, pipe)
			runtime->num_sprites[pipe] = 4;
	else if (DISPLAY_VER(dev_priv) >= 11)
		for_each_pipe(dev_priv, pipe)
			runtime->num_sprites[pipe] = 6;
	else if (DISPLAY_VER(dev_priv) == 10)
		for_each_pipe(dev_priv, pipe)
			runtime->num_sprites[pipe] = 3;
	else if (IS_BROXTON(dev_priv)) {
		/*
		 * Skylake and Broxton currently don't expose the topmost plane as its
		 * use is exclusive with the legacy cursor and we only want to expose
		 * one of those, not both. Until we can safely expose the topmost plane
		 * as a DRM_PLANE_TYPE_CURSOR with all the features exposed/supported,
		 * we don't expose the topmost plane at all to prevent ABI breakage
		 * down the line.
		 */

		runtime->num_sprites[PIPE_A] = 2;
		runtime->num_sprites[PIPE_B] = 2;
		runtime->num_sprites[PIPE_C] = 1;
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		for_each_pipe(dev_priv, pipe)
			runtime->num_sprites[pipe] = 2;
	} else if (DISPLAY_VER(dev_priv) >= 5 || IS_G4X(dev_priv)) {
		for_each_pipe(dev_priv, pipe)
			runtime->num_sprites[pipe] = 1;
	}

	if (HAS_DISPLAY(dev_priv) && IS_GRAPHICS_VER(dev_priv, 7, 8) &&
	    HAS_PCH_SPLIT(dev_priv)) {
		u32 fuse_strap = intel_de_read(dev_priv, FUSE_STRAP);
		u32 sfuse_strap = intel_de_read(dev_priv, SFUSE_STRAP);

		/*
		 * SFUSE_STRAP is supposed to have a bit signalling the display
		 * is fused off. Unfortunately it seems that, at least in
		 * certain cases, fused off display means that PCH display
		 * reads don't land anywhere. In that case, we read 0s.
		 *
		 * On CPT/PPT, we can detect this case as SFUSE_STRAP_FUSE_LOCK
		 * should be set when taking over after the firmware.
		 */
		if (fuse_strap & ILK_INTERNAL_DISPLAY_DISABLE ||
		    sfuse_strap & SFUSE_STRAP_DISPLAY_DISABLED ||
		    (HAS_PCH_CPT(dev_priv) &&
		     !(sfuse_strap & SFUSE_STRAP_FUSE_LOCK))) {
			drm_info(&dev_priv->drm,
				 "Display fused off, disabling\n");
			info->display.pipe_mask = 0;
			info->display.cpu_transcoder_mask = 0;
		} else if (fuse_strap & IVB_PIPE_C_DISABLE) {
			drm_info(&dev_priv->drm, "PipeC fused off\n");
			info->display.pipe_mask &= ~BIT(PIPE_C);
			info->display.cpu_transcoder_mask &= ~BIT(TRANSCODER_C);
		}
	} else if (HAS_DISPLAY(dev_priv) && DISPLAY_VER(dev_priv) >= 9) {
		u32 dfsm = intel_de_read(dev_priv, SKL_DFSM);

		if (dfsm & SKL_DFSM_PIPE_A_DISABLE) {
			info->display.pipe_mask &= ~BIT(PIPE_A);
			info->display.cpu_transcoder_mask &= ~BIT(TRANSCODER_A);
		}
		if (dfsm & SKL_DFSM_PIPE_B_DISABLE) {
			info->display.pipe_mask &= ~BIT(PIPE_B);
			info->display.cpu_transcoder_mask &= ~BIT(TRANSCODER_B);
		}
		if (dfsm & SKL_DFSM_PIPE_C_DISABLE) {
			info->display.pipe_mask &= ~BIT(PIPE_C);
			info->display.cpu_transcoder_mask &= ~BIT(TRANSCODER_C);
		}

		if (DISPLAY_VER(dev_priv) >= 12 &&
		    (dfsm & TGL_DFSM_PIPE_D_DISABLE)) {
			info->display.pipe_mask &= ~BIT(PIPE_D);
			info->display.cpu_transcoder_mask &= ~BIT(TRANSCODER_D);
		}

		if (dfsm & SKL_DFSM_DISPLAY_HDCP_DISABLE)
			info->display.has_hdcp = 0;

		if (dfsm & SKL_DFSM_DISPLAY_PM_DISABLE)
			info->display.has_fbc = 0;

		if (DISPLAY_VER(dev_priv) >= 11 && (dfsm & ICL_DFSM_DMC_DISABLE))
			info->display.has_dmc = 0;

		if (DISPLAY_VER(dev_priv) >= 10 &&
		    (dfsm & GLK_DFSM_DISPLAY_DSC_DISABLE))
			info->display.has_dsc = 0;
	}

	if (GRAPHICS_VER(dev_priv) == 6 && intel_vtd_active(dev_priv)) {
		drm_info(&dev_priv->drm,
			 "Disabling ppGTT for VT-d support\n");
		info->ppgtt_type = INTEL_PPGTT_NONE;
	}

	runtime->rawclk_freq = intel_read_rawclk(dev_priv);
	drm_dbg(&dev_priv->drm, "rawclk rate: %d kHz\n", runtime->rawclk_freq);

	if (!HAS_DISPLAY(dev_priv)) {
		dev_priv->drm.driver_features &= ~(DRIVER_MODESET |
						   DRIVER_ATOMIC);
		memset(&info->display, 0, sizeof(info->display));
		memset(runtime->num_sprites, 0, sizeof(runtime->num_sprites));
		memset(runtime->num_scalers, 0, sizeof(runtime->num_scalers));
	}
}

void intel_driver_caps_print(const struct intel_driver_caps *caps,
			     struct drm_printer *p)
{
	drm_printf(p, "Has logical contexts? %s\n",
		   yesno(caps->has_logical_contexts));
	drm_printf(p, "scheduler: %x\n", caps->scheduler);
}

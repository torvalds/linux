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

#include <linux/console.h>
#include <linux/vga_switcheroo.h>

#include <drm/drm_drv.h>
#include <drm/i915_pciids.h>

#include "display/intel_fbdev.h"

#include "i915_drv.h"
#include "i915_perf.h"
#include "i915_globals.h"
#include "i915_selftest.h"

#define PLATFORM(x) .platform = (x)
#define GEN(x) .gen = (x), .gen_mask = BIT((x) - 1)

#define I845_PIPE_OFFSETS \
	.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET,	\
	}, \
	.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
	}

#define I9XX_PIPE_OFFSETS \
	.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET,	\
		[TRANSCODER_B] = PIPE_B_OFFSET, \
	}, \
	.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
	}

#define IVB_PIPE_OFFSETS \
	.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET,	\
		[TRANSCODER_B] = PIPE_B_OFFSET, \
		[TRANSCODER_C] = PIPE_C_OFFSET, \
	}, \
	.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
		[TRANSCODER_C] = TRANSCODER_C_OFFSET, \
	}

#define HSW_PIPE_OFFSETS \
	.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET,	\
		[TRANSCODER_B] = PIPE_B_OFFSET, \
		[TRANSCODER_C] = PIPE_C_OFFSET, \
		[TRANSCODER_EDP] = PIPE_EDP_OFFSET, \
	}, \
	.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
		[TRANSCODER_C] = TRANSCODER_C_OFFSET, \
		[TRANSCODER_EDP] = TRANSCODER_EDP_OFFSET, \
	}

#define CHV_PIPE_OFFSETS \
	.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET, \
		[TRANSCODER_B] = PIPE_B_OFFSET, \
		[TRANSCODER_C] = CHV_PIPE_C_OFFSET, \
	}, \
	.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
		[TRANSCODER_C] = CHV_TRANSCODER_C_OFFSET, \
	}

#define I845_CURSOR_OFFSETS \
	.cursor_offsets = { \
		[PIPE_A] = CURSOR_A_OFFSET, \
	}

#define I9XX_CURSOR_OFFSETS \
	.cursor_offsets = { \
		[PIPE_A] = CURSOR_A_OFFSET, \
		[PIPE_B] = CURSOR_B_OFFSET, \
	}

#define CHV_CURSOR_OFFSETS \
	.cursor_offsets = { \
		[PIPE_A] = CURSOR_A_OFFSET, \
		[PIPE_B] = CURSOR_B_OFFSET, \
		[PIPE_C] = CHV_CURSOR_C_OFFSET, \
	}

#define IVB_CURSOR_OFFSETS \
	.cursor_offsets = { \
		[PIPE_A] = CURSOR_A_OFFSET, \
		[PIPE_B] = IVB_CURSOR_B_OFFSET, \
		[PIPE_C] = IVB_CURSOR_C_OFFSET, \
	}

#define TGL_CURSOR_OFFSETS \
	.cursor_offsets = { \
		[PIPE_A] = CURSOR_A_OFFSET, \
		[PIPE_B] = IVB_CURSOR_B_OFFSET, \
		[PIPE_C] = IVB_CURSOR_C_OFFSET, \
		[PIPE_D] = TGL_CURSOR_D_OFFSET, \
	}

#define I9XX_COLORS \
	.color = { .gamma_lut_size = 256 }
#define I965_COLORS \
	.color = { .gamma_lut_size = 129, \
		   .gamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
	}
#define ILK_COLORS \
	.color = { .gamma_lut_size = 1024 }
#define IVB_COLORS \
	.color = { .degamma_lut_size = 1024, .gamma_lut_size = 1024 }
#define CHV_COLORS \
	.color = { .degamma_lut_size = 65, .gamma_lut_size = 257, \
		   .degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
		   .gamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
	}
#define GLK_COLORS \
	.color = { .degamma_lut_size = 33, .gamma_lut_size = 1024, \
		   .degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING | \
					DRM_COLOR_LUT_EQUAL_CHANNELS, \
	}

/* Keep in gen based order, and chronological order within a gen */

#define GEN_DEFAULT_PAGE_SIZES \
	.page_sizes = I915_GTT_PAGE_SIZE_4K

#define GEN_DEFAULT_REGIONS \
	.memory_regions = REGION_SMEM | REGION_STOLEN

#define I830_FEATURES \
	GEN(2), \
	.is_mobile = 1, \
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.display.has_overlay = 1, \
	.display.cursor_needs_physical = 1, \
	.display.overlay_needs_physical = 1, \
	.display.has_gmch = 1, \
	.gpu_reset_clobbers_display = true, \
	.hws_needs_physical = 1, \
	.unfenced_needs_alignment = 1, \
	.platform_engine_mask = BIT(RCS0), \
	.has_snoop = true, \
	.has_coherent_ggtt = false, \
	.dma_mask_size = 32, \
	I9XX_PIPE_OFFSETS, \
	I9XX_CURSOR_OFFSETS, \
	I9XX_COLORS, \
	GEN_DEFAULT_PAGE_SIZES, \
	GEN_DEFAULT_REGIONS

#define I845_FEATURES \
	GEN(2), \
	.pipe_mask = BIT(PIPE_A), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A), \
	.display.has_overlay = 1, \
	.display.overlay_needs_physical = 1, \
	.display.has_gmch = 1, \
	.gpu_reset_clobbers_display = true, \
	.hws_needs_physical = 1, \
	.unfenced_needs_alignment = 1, \
	.platform_engine_mask = BIT(RCS0), \
	.has_snoop = true, \
	.has_coherent_ggtt = false, \
	.dma_mask_size = 32, \
	I845_PIPE_OFFSETS, \
	I845_CURSOR_OFFSETS, \
	I9XX_COLORS, \
	GEN_DEFAULT_PAGE_SIZES, \
	GEN_DEFAULT_REGIONS

static const struct intel_device_info i830_info = {
	I830_FEATURES,
	PLATFORM(INTEL_I830),
};

static const struct intel_device_info i845g_info = {
	I845_FEATURES,
	PLATFORM(INTEL_I845G),
};

static const struct intel_device_info i85x_info = {
	I830_FEATURES,
	PLATFORM(INTEL_I85X),
	.display.has_fbc = 1,
};

static const struct intel_device_info i865g_info = {
	I845_FEATURES,
	PLATFORM(INTEL_I865G),
	.display.has_fbc = 1,
};

#define GEN3_FEATURES \
	GEN(3), \
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.display.has_gmch = 1, \
	.gpu_reset_clobbers_display = true, \
	.platform_engine_mask = BIT(RCS0), \
	.has_snoop = true, \
	.has_coherent_ggtt = true, \
	.dma_mask_size = 32, \
	I9XX_PIPE_OFFSETS, \
	I9XX_CURSOR_OFFSETS, \
	I9XX_COLORS, \
	GEN_DEFAULT_PAGE_SIZES, \
	GEN_DEFAULT_REGIONS

static const struct intel_device_info i915g_info = {
	GEN3_FEATURES,
	PLATFORM(INTEL_I915G),
	.has_coherent_ggtt = false,
	.display.cursor_needs_physical = 1,
	.display.has_overlay = 1,
	.display.overlay_needs_physical = 1,
	.hws_needs_physical = 1,
	.unfenced_needs_alignment = 1,
};

static const struct intel_device_info i915gm_info = {
	GEN3_FEATURES,
	PLATFORM(INTEL_I915GM),
	.is_mobile = 1,
	.display.cursor_needs_physical = 1,
	.display.has_overlay = 1,
	.display.overlay_needs_physical = 1,
	.display.supports_tv = 1,
	.display.has_fbc = 1,
	.hws_needs_physical = 1,
	.unfenced_needs_alignment = 1,
};

static const struct intel_device_info i945g_info = {
	GEN3_FEATURES,
	PLATFORM(INTEL_I945G),
	.display.has_hotplug = 1,
	.display.cursor_needs_physical = 1,
	.display.has_overlay = 1,
	.display.overlay_needs_physical = 1,
	.hws_needs_physical = 1,
	.unfenced_needs_alignment = 1,
};

static const struct intel_device_info i945gm_info = {
	GEN3_FEATURES,
	PLATFORM(INTEL_I945GM),
	.is_mobile = 1,
	.display.has_hotplug = 1,
	.display.cursor_needs_physical = 1,
	.display.has_overlay = 1,
	.display.overlay_needs_physical = 1,
	.display.supports_tv = 1,
	.display.has_fbc = 1,
	.hws_needs_physical = 1,
	.unfenced_needs_alignment = 1,
};

static const struct intel_device_info g33_info = {
	GEN3_FEATURES,
	PLATFORM(INTEL_G33),
	.display.has_hotplug = 1,
	.display.has_overlay = 1,
	.dma_mask_size = 36,
};

static const struct intel_device_info pnv_g_info = {
	GEN3_FEATURES,
	PLATFORM(INTEL_PINEVIEW),
	.display.has_hotplug = 1,
	.display.has_overlay = 1,
	.dma_mask_size = 36,
};

static const struct intel_device_info pnv_m_info = {
	GEN3_FEATURES,
	PLATFORM(INTEL_PINEVIEW),
	.is_mobile = 1,
	.display.has_hotplug = 1,
	.display.has_overlay = 1,
	.dma_mask_size = 36,
};

#define GEN4_FEATURES \
	GEN(4), \
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.display.has_hotplug = 1, \
	.display.has_gmch = 1, \
	.gpu_reset_clobbers_display = true, \
	.platform_engine_mask = BIT(RCS0), \
	.has_snoop = true, \
	.has_coherent_ggtt = true, \
	.dma_mask_size = 36, \
	I9XX_PIPE_OFFSETS, \
	I9XX_CURSOR_OFFSETS, \
	I965_COLORS, \
	GEN_DEFAULT_PAGE_SIZES, \
	GEN_DEFAULT_REGIONS

static const struct intel_device_info i965g_info = {
	GEN4_FEATURES,
	PLATFORM(INTEL_I965G),
	.display.has_overlay = 1,
	.hws_needs_physical = 1,
	.has_snoop = false,
};

static const struct intel_device_info i965gm_info = {
	GEN4_FEATURES,
	PLATFORM(INTEL_I965GM),
	.is_mobile = 1,
	.display.has_fbc = 1,
	.display.has_overlay = 1,
	.display.supports_tv = 1,
	.hws_needs_physical = 1,
	.has_snoop = false,
};

static const struct intel_device_info g45_info = {
	GEN4_FEATURES,
	PLATFORM(INTEL_G45),
	.platform_engine_mask = BIT(RCS0) | BIT(VCS0),
	.gpu_reset_clobbers_display = false,
};

static const struct intel_device_info gm45_info = {
	GEN4_FEATURES,
	PLATFORM(INTEL_GM45),
	.is_mobile = 1,
	.display.has_fbc = 1,
	.display.supports_tv = 1,
	.platform_engine_mask = BIT(RCS0) | BIT(VCS0),
	.gpu_reset_clobbers_display = false,
};

#define GEN5_FEATURES \
	GEN(5), \
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.display.has_hotplug = 1, \
	.platform_engine_mask = BIT(RCS0) | BIT(VCS0), \
	.has_snoop = true, \
	.has_coherent_ggtt = true, \
	/* ilk does support rc6, but we do not implement [power] contexts */ \
	.has_rc6 = 0, \
	.dma_mask_size = 36, \
	I9XX_PIPE_OFFSETS, \
	I9XX_CURSOR_OFFSETS, \
	ILK_COLORS, \
	GEN_DEFAULT_PAGE_SIZES, \
	GEN_DEFAULT_REGIONS

static const struct intel_device_info ilk_d_info = {
	GEN5_FEATURES,
	PLATFORM(INTEL_IRONLAKE),
};

static const struct intel_device_info ilk_m_info = {
	GEN5_FEATURES,
	PLATFORM(INTEL_IRONLAKE),
	.is_mobile = 1,
	.has_rps = true,
	.display.has_fbc = 1,
};

#define GEN6_FEATURES \
	GEN(6), \
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.display.has_hotplug = 1, \
	.display.has_fbc = 1, \
	.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0), \
	.has_coherent_ggtt = true, \
	.has_llc = 1, \
	.has_rc6 = 1, \
	.has_rc6p = 1, \
	.has_rps = true, \
	.dma_mask_size = 40, \
	.ppgtt_type = INTEL_PPGTT_ALIASING, \
	.ppgtt_size = 31, \
	I9XX_PIPE_OFFSETS, \
	I9XX_CURSOR_OFFSETS, \
	ILK_COLORS, \
	GEN_DEFAULT_PAGE_SIZES, \
	GEN_DEFAULT_REGIONS

#define SNB_D_PLATFORM \
	GEN6_FEATURES, \
	PLATFORM(INTEL_SANDYBRIDGE)

static const struct intel_device_info snb_d_gt1_info = {
	SNB_D_PLATFORM,
	.gt = 1,
};

static const struct intel_device_info snb_d_gt2_info = {
	SNB_D_PLATFORM,
	.gt = 2,
};

#define SNB_M_PLATFORM \
	GEN6_FEATURES, \
	PLATFORM(INTEL_SANDYBRIDGE), \
	.is_mobile = 1


static const struct intel_device_info snb_m_gt1_info = {
	SNB_M_PLATFORM,
	.gt = 1,
};

static const struct intel_device_info snb_m_gt2_info = {
	SNB_M_PLATFORM,
	.gt = 2,
};

#define GEN7_FEATURES  \
	GEN(7), \
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | BIT(TRANSCODER_C), \
	.display.has_hotplug = 1, \
	.display.has_fbc = 1, \
	.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0), \
	.has_coherent_ggtt = true, \
	.has_llc = 1, \
	.has_rc6 = 1, \
	.has_rc6p = 1, \
	.has_reset_engine = true, \
	.has_rps = true, \
	.dma_mask_size = 40, \
	.ppgtt_type = INTEL_PPGTT_ALIASING, \
	.ppgtt_size = 31, \
	IVB_PIPE_OFFSETS, \
	IVB_CURSOR_OFFSETS, \
	IVB_COLORS, \
	GEN_DEFAULT_PAGE_SIZES, \
	GEN_DEFAULT_REGIONS

#define IVB_D_PLATFORM \
	GEN7_FEATURES, \
	PLATFORM(INTEL_IVYBRIDGE), \
	.has_l3_dpf = 1

static const struct intel_device_info ivb_d_gt1_info = {
	IVB_D_PLATFORM,
	.gt = 1,
};

static const struct intel_device_info ivb_d_gt2_info = {
	IVB_D_PLATFORM,
	.gt = 2,
};

#define IVB_M_PLATFORM \
	GEN7_FEATURES, \
	PLATFORM(INTEL_IVYBRIDGE), \
	.is_mobile = 1, \
	.has_l3_dpf = 1

static const struct intel_device_info ivb_m_gt1_info = {
	IVB_M_PLATFORM,
	.gt = 1,
};

static const struct intel_device_info ivb_m_gt2_info = {
	IVB_M_PLATFORM,
	.gt = 2,
};

static const struct intel_device_info ivb_q_info = {
	GEN7_FEATURES,
	PLATFORM(INTEL_IVYBRIDGE),
	.gt = 2,
	.pipe_mask = 0, /* legal, last one wins */
	.cpu_transcoder_mask = 0,
	.has_l3_dpf = 1,
};

static const struct intel_device_info vlv_info = {
	PLATFORM(INTEL_VALLEYVIEW),
	GEN(7),
	.is_lp = 1,
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B),
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B),
	.has_runtime_pm = 1,
	.has_rc6 = 1,
	.has_reset_engine = true,
	.has_rps = true,
	.display.has_gmch = 1,
	.display.has_hotplug = 1,
	.dma_mask_size = 40,
	.ppgtt_type = INTEL_PPGTT_ALIASING,
	.ppgtt_size = 31,
	.has_snoop = true,
	.has_coherent_ggtt = false,
	.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0),
	.display_mmio_offset = VLV_DISPLAY_BASE,
	I9XX_PIPE_OFFSETS,
	I9XX_CURSOR_OFFSETS,
	I965_COLORS,
	GEN_DEFAULT_PAGE_SIZES,
	GEN_DEFAULT_REGIONS,
};

#define G75_FEATURES  \
	GEN7_FEATURES, \
	.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP), \
	.display.has_ddi = 1, \
	.has_fpga_dbg = 1, \
	.display.has_psr = 1, \
	.display.has_psr_hw_tracking = 1, \
	.display.has_dp_mst = 1, \
	.has_rc6p = 0 /* RC6p removed-by HSW */, \
	HSW_PIPE_OFFSETS, \
	.has_runtime_pm = 1

#define HSW_PLATFORM \
	G75_FEATURES, \
	PLATFORM(INTEL_HASWELL), \
	.has_l3_dpf = 1

static const struct intel_device_info hsw_gt1_info = {
	HSW_PLATFORM,
	.gt = 1,
};

static const struct intel_device_info hsw_gt2_info = {
	HSW_PLATFORM,
	.gt = 2,
};

static const struct intel_device_info hsw_gt3_info = {
	HSW_PLATFORM,
	.gt = 3,
};

#define GEN8_FEATURES \
	G75_FEATURES, \
	GEN(8), \
	.has_logical_ring_contexts = 1, \
	.dma_mask_size = 39, \
	.ppgtt_type = INTEL_PPGTT_FULL, \
	.ppgtt_size = 48, \
	.has_64bit_reloc = 1

#define BDW_PLATFORM \
	GEN8_FEATURES, \
	PLATFORM(INTEL_BROADWELL)

static const struct intel_device_info bdw_gt1_info = {
	BDW_PLATFORM,
	.gt = 1,
};

static const struct intel_device_info bdw_gt2_info = {
	BDW_PLATFORM,
	.gt = 2,
};

static const struct intel_device_info bdw_rsvd_info = {
	BDW_PLATFORM,
	.gt = 3,
	/* According to the device ID those devices are GT3, they were
	 * previously treated as not GT3, keep it like that.
	 */
};

static const struct intel_device_info bdw_gt3_info = {
	BDW_PLATFORM,
	.gt = 3,
	.platform_engine_mask =
		BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS1),
};

static const struct intel_device_info chv_info = {
	PLATFORM(INTEL_CHERRYVIEW),
	GEN(8),
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | BIT(TRANSCODER_C),
	.display.has_hotplug = 1,
	.is_lp = 1,
	.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0),
	.has_64bit_reloc = 1,
	.has_runtime_pm = 1,
	.has_rc6 = 1,
	.has_rps = true,
	.has_logical_ring_contexts = 1,
	.display.has_gmch = 1,
	.dma_mask_size = 39,
	.ppgtt_type = INTEL_PPGTT_FULL,
	.ppgtt_size = 32,
	.has_reset_engine = 1,
	.has_snoop = true,
	.has_coherent_ggtt = false,
	.display_mmio_offset = VLV_DISPLAY_BASE,
	CHV_PIPE_OFFSETS,
	CHV_CURSOR_OFFSETS,
	CHV_COLORS,
	GEN_DEFAULT_PAGE_SIZES,
	GEN_DEFAULT_REGIONS,
};

#define GEN9_DEFAULT_PAGE_SIZES \
	.page_sizes = I915_GTT_PAGE_SIZE_4K | \
		      I915_GTT_PAGE_SIZE_64K

#define GEN9_FEATURES \
	GEN8_FEATURES, \
	GEN(9), \
	GEN9_DEFAULT_PAGE_SIZES, \
	.display.has_csr = 1, \
	.has_gt_uc = 1, \
	.display.has_hdcp = 1, \
	.display.has_ipc = 1, \
	.ddb_size = 896, \
	.num_supported_dbuf_slices = 1

#define SKL_PLATFORM \
	GEN9_FEATURES, \
	PLATFORM(INTEL_SKYLAKE)

static const struct intel_device_info skl_gt1_info = {
	SKL_PLATFORM,
	.gt = 1,
};

static const struct intel_device_info skl_gt2_info = {
	SKL_PLATFORM,
	.gt = 2,
};

#define SKL_GT3_PLUS_PLATFORM \
	SKL_PLATFORM, \
	.platform_engine_mask = \
		BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS1)


static const struct intel_device_info skl_gt3_info = {
	SKL_GT3_PLUS_PLATFORM,
	.gt = 3,
};

static const struct intel_device_info skl_gt4_info = {
	SKL_GT3_PLUS_PLATFORM,
	.gt = 4,
};

#define GEN9_LP_FEATURES \
	GEN(9), \
	.is_lp = 1, \
	.num_supported_dbuf_slices = 1, \
	.display.has_hotplug = 1, \
	.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0), \
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP) | \
		BIT(TRANSCODER_DSI_A) | BIT(TRANSCODER_DSI_C), \
	.has_64bit_reloc = 1, \
	.display.has_ddi = 1, \
	.has_fpga_dbg = 1, \
	.display.has_fbc = 1, \
	.display.has_hdcp = 1, \
	.display.has_psr = 1, \
	.display.has_psr_hw_tracking = 1, \
	.has_runtime_pm = 1, \
	.display.has_csr = 1, \
	.has_rc6 = 1, \
	.has_rps = true, \
	.display.has_dp_mst = 1, \
	.has_logical_ring_contexts = 1, \
	.has_gt_uc = 1, \
	.dma_mask_size = 39, \
	.ppgtt_type = INTEL_PPGTT_FULL, \
	.ppgtt_size = 48, \
	.has_reset_engine = 1, \
	.has_snoop = true, \
	.has_coherent_ggtt = false, \
	.display.has_ipc = 1, \
	HSW_PIPE_OFFSETS, \
	IVB_CURSOR_OFFSETS, \
	IVB_COLORS, \
	GEN9_DEFAULT_PAGE_SIZES, \
	GEN_DEFAULT_REGIONS

static const struct intel_device_info bxt_info = {
	GEN9_LP_FEATURES,
	PLATFORM(INTEL_BROXTON),
	.ddb_size = 512,
};

static const struct intel_device_info glk_info = {
	GEN9_LP_FEATURES,
	PLATFORM(INTEL_GEMINILAKE),
	.ddb_size = 1024,
	GLK_COLORS,
};

#define KBL_PLATFORM \
	GEN9_FEATURES, \
	PLATFORM(INTEL_KABYLAKE)

static const struct intel_device_info kbl_gt1_info = {
	KBL_PLATFORM,
	.gt = 1,
};

static const struct intel_device_info kbl_gt2_info = {
	KBL_PLATFORM,
	.gt = 2,
};

static const struct intel_device_info kbl_gt3_info = {
	KBL_PLATFORM,
	.gt = 3,
	.platform_engine_mask =
		BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS1),
};

#define CFL_PLATFORM \
	GEN9_FEATURES, \
	PLATFORM(INTEL_COFFEELAKE)

static const struct intel_device_info cfl_gt1_info = {
	CFL_PLATFORM,
	.gt = 1,
};

static const struct intel_device_info cfl_gt2_info = {
	CFL_PLATFORM,
	.gt = 2,
};

static const struct intel_device_info cfl_gt3_info = {
	CFL_PLATFORM,
	.gt = 3,
	.platform_engine_mask =
		BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS1),
};

#define CML_PLATFORM \
	GEN9_FEATURES, \
	PLATFORM(INTEL_COMETLAKE)

static const struct intel_device_info cml_gt1_info = {
	CML_PLATFORM,
	.gt = 1,
};

static const struct intel_device_info cml_gt2_info = {
	CML_PLATFORM,
	.gt = 2,
};

#define GEN10_FEATURES \
	GEN9_FEATURES, \
	GEN(10), \
	.ddb_size = 1024, \
	.display.has_dsc = 1, \
	.has_coherent_ggtt = false, \
	GLK_COLORS

static const struct intel_device_info cnl_info = {
	GEN10_FEATURES,
	PLATFORM(INTEL_CANNONLAKE),
	.gt = 2,
};

#define GEN11_DEFAULT_PAGE_SIZES \
	.page_sizes = I915_GTT_PAGE_SIZE_4K | \
		      I915_GTT_PAGE_SIZE_64K | \
		      I915_GTT_PAGE_SIZE_2M

#define GEN11_FEATURES \
	GEN10_FEATURES, \
	GEN11_DEFAULT_PAGE_SIZES, \
	.abox_mask = BIT(0), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP) | \
		BIT(TRANSCODER_DSI_0) | BIT(TRANSCODER_DSI_1), \
	.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET, \
		[TRANSCODER_B] = PIPE_B_OFFSET, \
		[TRANSCODER_C] = PIPE_C_OFFSET, \
		[TRANSCODER_EDP] = PIPE_EDP_OFFSET, \
		[TRANSCODER_DSI_0] = PIPE_DSI0_OFFSET, \
		[TRANSCODER_DSI_1] = PIPE_DSI1_OFFSET, \
	}, \
	.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
		[TRANSCODER_C] = TRANSCODER_C_OFFSET, \
		[TRANSCODER_EDP] = TRANSCODER_EDP_OFFSET, \
		[TRANSCODER_DSI_0] = TRANSCODER_DSI0_OFFSET, \
		[TRANSCODER_DSI_1] = TRANSCODER_DSI1_OFFSET, \
	}, \
	GEN(11), \
	.ddb_size = 2048, \
	.num_supported_dbuf_slices = 2, \
	.has_logical_ring_elsq = 1, \
	.color = { .degamma_lut_size = 33, .gamma_lut_size = 262145 }

static const struct intel_device_info icl_info = {
	GEN11_FEATURES,
	PLATFORM(INTEL_ICELAKE),
	.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS0) | BIT(VCS2),
};

static const struct intel_device_info ehl_info = {
	GEN11_FEATURES,
	PLATFORM(INTEL_ELKHARTLAKE),
	.require_force_probe = 1,
	.platform_engine_mask = BIT(RCS0) | BIT(BCS0) | BIT(VCS0) | BIT(VECS0),
	.ppgtt_size = 36,
};

static const struct intel_device_info jsl_info = {
	GEN11_FEATURES,
	PLATFORM(INTEL_JASPERLAKE),
	.require_force_probe = 1,
	.platform_engine_mask = BIT(RCS0) | BIT(BCS0) | BIT(VCS0) | BIT(VECS0),
	.ppgtt_size = 36,
};

#define GEN12_FEATURES \
	GEN11_FEATURES, \
	GEN(12), \
	.abox_mask = GENMASK(2, 1), \
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D), \
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D) | \
		BIT(TRANSCODER_DSI_0) | BIT(TRANSCODER_DSI_1), \
	.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET, \
		[TRANSCODER_B] = PIPE_B_OFFSET, \
		[TRANSCODER_C] = PIPE_C_OFFSET, \
		[TRANSCODER_D] = PIPE_D_OFFSET, \
		[TRANSCODER_DSI_0] = PIPE_DSI0_OFFSET, \
		[TRANSCODER_DSI_1] = PIPE_DSI1_OFFSET, \
	}, \
	.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
		[TRANSCODER_C] = TRANSCODER_C_OFFSET, \
		[TRANSCODER_D] = TRANSCODER_D_OFFSET, \
		[TRANSCODER_DSI_0] = TRANSCODER_DSI0_OFFSET, \
		[TRANSCODER_DSI_1] = TRANSCODER_DSI1_OFFSET, \
	}, \
	TGL_CURSOR_OFFSETS, \
	.has_global_mocs = 1, \
	.display.has_dsb = 1

static const struct intel_device_info tgl_info = {
	GEN12_FEATURES,
	PLATFORM(INTEL_TIGERLAKE),
	.display.has_modular_fia = 1,
	.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS0) | BIT(VCS2),
};

static const struct intel_device_info rkl_info = {
	GEN12_FEATURES,
	PLATFORM(INTEL_ROCKETLAKE),
	.abox_mask = BIT(0),
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
	.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C),
	.require_force_probe = 1,
	.display.has_hti = 1,
	.display.has_psr_hw_tracking = 0,
	.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS0),
};

#define GEN12_DGFX_FEATURES \
	GEN12_FEATURES, \
	.memory_regions = REGION_SMEM | REGION_LMEM, \
	.has_master_unit_irq = 1, \
	.has_llc = 0, \
	.has_snoop = 1, \
	.is_dgfx = 1

static const struct intel_device_info dg1_info __maybe_unused = {
	GEN12_DGFX_FEATURES,
	PLATFORM(INTEL_DG1),
	.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D),
	.require_force_probe = 1,
	.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) | BIT(VECS0) |
		BIT(VCS0) | BIT(VCS2),
	/* Wa_16011227922 */
	.ppgtt_size = 47,
};

#undef GEN
#undef PLATFORM

/*
 * Make sure any device matches here are from most specific to most
 * general.  For example, since the Quanta match is based on the subsystem
 * and subvendor IDs, we need it to come before the more general IVB
 * PCI ID matches, otherwise we'll use the wrong info struct above.
 */
static const struct pci_device_id pciidlist[] = {
	INTEL_I830_IDS(&i830_info),
	INTEL_I845G_IDS(&i845g_info),
	INTEL_I85X_IDS(&i85x_info),
	INTEL_I865G_IDS(&i865g_info),
	INTEL_I915G_IDS(&i915g_info),
	INTEL_I915GM_IDS(&i915gm_info),
	INTEL_I945G_IDS(&i945g_info),
	INTEL_I945GM_IDS(&i945gm_info),
	INTEL_I965G_IDS(&i965g_info),
	INTEL_G33_IDS(&g33_info),
	INTEL_I965GM_IDS(&i965gm_info),
	INTEL_GM45_IDS(&gm45_info),
	INTEL_G45_IDS(&g45_info),
	INTEL_PINEVIEW_G_IDS(&pnv_g_info),
	INTEL_PINEVIEW_M_IDS(&pnv_m_info),
	INTEL_IRONLAKE_D_IDS(&ilk_d_info),
	INTEL_IRONLAKE_M_IDS(&ilk_m_info),
	INTEL_SNB_D_GT1_IDS(&snb_d_gt1_info),
	INTEL_SNB_D_GT2_IDS(&snb_d_gt2_info),
	INTEL_SNB_M_GT1_IDS(&snb_m_gt1_info),
	INTEL_SNB_M_GT2_IDS(&snb_m_gt2_info),
	INTEL_IVB_Q_IDS(&ivb_q_info), /* must be first IVB */
	INTEL_IVB_M_GT1_IDS(&ivb_m_gt1_info),
	INTEL_IVB_M_GT2_IDS(&ivb_m_gt2_info),
	INTEL_IVB_D_GT1_IDS(&ivb_d_gt1_info),
	INTEL_IVB_D_GT2_IDS(&ivb_d_gt2_info),
	INTEL_HSW_GT1_IDS(&hsw_gt1_info),
	INTEL_HSW_GT2_IDS(&hsw_gt2_info),
	INTEL_HSW_GT3_IDS(&hsw_gt3_info),
	INTEL_VLV_IDS(&vlv_info),
	INTEL_BDW_GT1_IDS(&bdw_gt1_info),
	INTEL_BDW_GT2_IDS(&bdw_gt2_info),
	INTEL_BDW_GT3_IDS(&bdw_gt3_info),
	INTEL_BDW_RSVD_IDS(&bdw_rsvd_info),
	INTEL_CHV_IDS(&chv_info),
	INTEL_SKL_GT1_IDS(&skl_gt1_info),
	INTEL_SKL_GT2_IDS(&skl_gt2_info),
	INTEL_SKL_GT3_IDS(&skl_gt3_info),
	INTEL_SKL_GT4_IDS(&skl_gt4_info),
	INTEL_BXT_IDS(&bxt_info),
	INTEL_GLK_IDS(&glk_info),
	INTEL_KBL_GT1_IDS(&kbl_gt1_info),
	INTEL_KBL_GT2_IDS(&kbl_gt2_info),
	INTEL_KBL_GT3_IDS(&kbl_gt3_info),
	INTEL_KBL_GT4_IDS(&kbl_gt3_info),
	INTEL_AML_KBL_GT2_IDS(&kbl_gt2_info),
	INTEL_CFL_S_GT1_IDS(&cfl_gt1_info),
	INTEL_CFL_S_GT2_IDS(&cfl_gt2_info),
	INTEL_CFL_H_GT1_IDS(&cfl_gt1_info),
	INTEL_CFL_H_GT2_IDS(&cfl_gt2_info),
	INTEL_CFL_U_GT2_IDS(&cfl_gt2_info),
	INTEL_CFL_U_GT3_IDS(&cfl_gt3_info),
	INTEL_WHL_U_GT1_IDS(&cfl_gt1_info),
	INTEL_WHL_U_GT2_IDS(&cfl_gt2_info),
	INTEL_AML_CFL_GT2_IDS(&cfl_gt2_info),
	INTEL_WHL_U_GT3_IDS(&cfl_gt3_info),
	INTEL_CML_GT1_IDS(&cml_gt1_info),
	INTEL_CML_GT2_IDS(&cml_gt2_info),
	INTEL_CML_U_GT1_IDS(&cml_gt1_info),
	INTEL_CML_U_GT2_IDS(&cml_gt2_info),
	INTEL_CNL_IDS(&cnl_info),
	INTEL_ICL_11_IDS(&icl_info),
	INTEL_EHL_IDS(&ehl_info),
	INTEL_JSL_IDS(&jsl_info),
	INTEL_TGL_12_IDS(&tgl_info),
	INTEL_RKL_IDS(&rkl_info),
	{0, 0, 0}
};
MODULE_DEVICE_TABLE(pci, pciidlist);

static void i915_pci_remove(struct pci_dev *pdev)
{
	struct drm_i915_private *i915;

	i915 = pci_get_drvdata(pdev);
	if (!i915) /* driver load aborted, nothing to cleanup */
		return;

	i915_driver_remove(i915);
	pci_set_drvdata(pdev, NULL);
}

/* is device_id present in comma separated list of ids */
static bool force_probe(u16 device_id, const char *devices)
{
	char *s, *p, *tok;
	bool ret;

	if (!devices || !*devices)
		return false;

	/* match everything */
	if (strcmp(devices, "*") == 0)
		return true;

	s = kstrdup(devices, GFP_KERNEL);
	if (!s)
		return false;

	for (p = s, ret = false; (tok = strsep(&p, ",")) != NULL; ) {
		u16 val;

		if (kstrtou16(tok, 16, &val) == 0 && val == device_id) {
			ret = true;
			break;
		}
	}

	kfree(s);

	return ret;
}

static int i915_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct intel_device_info *intel_info =
		(struct intel_device_info *) ent->driver_data;
	int err;

	if (intel_info->require_force_probe &&
	    !force_probe(pdev->device, i915_modparams.force_probe)) {
		dev_info(&pdev->dev,
			 "Your graphics device %04x is not properly supported by the driver in this\n"
			 "kernel version. To force driver probe anyway, use i915.force_probe=%04x\n"
			 "module parameter or CONFIG_DRM_I915_FORCE_PROBE=%04x configuration option,\n"
			 "or (recommended) check for kernel updates.\n",
			 pdev->device, pdev->device, pdev->device);
		return -ENODEV;
	}

	/* Only bind to function 0 of the device. Early generations
	 * used function 1 as a placeholder for multi-head. This causes
	 * us confusion instead, especially on the systems where both
	 * functions have the same PCI-ID!
	 */
	if (PCI_FUNC(pdev->devfn))
		return -ENODEV;

	/*
	 * apple-gmux is needed on dual GPU MacBook Pro
	 * to probe the panel if we're the inactive GPU.
	 */
	if (vga_switcheroo_client_probe_defer(pdev))
		return -EPROBE_DEFER;

	err = i915_driver_probe(pdev, ent);
	if (err)
		return err;

	if (i915_inject_probe_failure(pci_get_drvdata(pdev))) {
		i915_pci_remove(pdev);
		return -ENODEV;
	}

	err = i915_live_selftests(pdev);
	if (err) {
		i915_pci_remove(pdev);
		return err > 0 ? -ENOTTY : err;
	}

	err = i915_perf_selftests(pdev);
	if (err) {
		i915_pci_remove(pdev);
		return err > 0 ? -ENOTTY : err;
	}

	return 0;
}

static void i915_pci_shutdown(struct pci_dev *pdev)
{
	struct drm_i915_private *i915 = pci_get_drvdata(pdev);

	i915_driver_shutdown(i915);
}

static struct pci_driver i915_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = i915_pci_probe,
	.remove = i915_pci_remove,
	.shutdown = i915_pci_shutdown,
	.driver.pm = &i915_pm_ops,
};

static int __init i915_init(void)
{
	bool use_kms = true;
	int err;

	err = i915_globals_init();
	if (err)
		return err;

	err = i915_mock_selftests();
	if (err)
		return err > 0 ? 0 : err;

	/*
	 * Enable KMS by default, unless explicitly overriden by
	 * either the i915.modeset prarameter or by the
	 * vga_text_mode_force boot option.
	 */

	if (i915_modparams.modeset == 0)
		use_kms = false;

	if (vgacon_text_force() && i915_modparams.modeset == -1)
		use_kms = false;

	if (!use_kms) {
		/* Silently fail loading to not upset userspace. */
		DRM_DEBUG_DRIVER("KMS disabled.\n");
		return 0;
	}

	i915_pmu_init();

	err = pci_register_driver(&i915_pci_driver);
	if (err) {
		i915_pmu_exit();
		return err;
	}

	i915_perf_sysctl_register();
	return 0;
}

static void __exit i915_exit(void)
{
	if (!i915_pci_driver.driver.owner)
		return;

	i915_perf_sysctl_unregister();
	pci_unregister_driver(&i915_pci_driver);
	i915_globals_exit();
	i915_pmu_exit();
}

module_init(i915_init);
module_exit(i915_exit);

MODULE_AUTHOR("Tungsten Graphics, Inc.");
MODULE_AUTHOR("Intel Corporation");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");

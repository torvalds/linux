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

#include <drm/drm_color_mgmt.h>
#include <drm/drm_drv.h>
#include <drm/i915_pciids.h>

#include "gt/intel_gt_regs.h"
#include "gt/intel_sa_media.h"

#include "i915_driver.h"
#include "i915_drv.h"
#include "i915_pci.h"
#include "i915_reg.h"
#include "intel_pci_config.h"

#define PLATFORM(x) .platform = (x)
#define GEN(x) \
	.__runtime.graphics.ip.ver = (x), \
	.__runtime.media.ip.ver = (x), \
	.__runtime.display.ip.ver = (x)

#define NO_DISPLAY .__runtime.pipe_mask = 0

#define I845_PIPE_OFFSETS \
	.display.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET,	\
	}, \
	.display.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
	}

#define I9XX_PIPE_OFFSETS \
	.display.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET,	\
		[TRANSCODER_B] = PIPE_B_OFFSET, \
	}, \
	.display.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
	}

#define IVB_PIPE_OFFSETS \
	.display.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET,	\
		[TRANSCODER_B] = PIPE_B_OFFSET, \
		[TRANSCODER_C] = PIPE_C_OFFSET, \
	}, \
	.display.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
		[TRANSCODER_C] = TRANSCODER_C_OFFSET, \
	}

#define HSW_PIPE_OFFSETS \
	.display.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET,	\
		[TRANSCODER_B] = PIPE_B_OFFSET, \
		[TRANSCODER_C] = PIPE_C_OFFSET, \
		[TRANSCODER_EDP] = PIPE_EDP_OFFSET, \
	}, \
	.display.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
		[TRANSCODER_C] = TRANSCODER_C_OFFSET, \
		[TRANSCODER_EDP] = TRANSCODER_EDP_OFFSET, \
	}

#define CHV_PIPE_OFFSETS \
	.display.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET, \
		[TRANSCODER_B] = PIPE_B_OFFSET, \
		[TRANSCODER_C] = CHV_PIPE_C_OFFSET, \
	}, \
	.display.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
		[TRANSCODER_C] = CHV_TRANSCODER_C_OFFSET, \
	}

#define I845_CURSOR_OFFSETS \
	.display.cursor_offsets = { \
		[PIPE_A] = CURSOR_A_OFFSET, \
	}

#define I9XX_CURSOR_OFFSETS \
	.display.cursor_offsets = { \
		[PIPE_A] = CURSOR_A_OFFSET, \
		[PIPE_B] = CURSOR_B_OFFSET, \
	}

#define CHV_CURSOR_OFFSETS \
	.display.cursor_offsets = { \
		[PIPE_A] = CURSOR_A_OFFSET, \
		[PIPE_B] = CURSOR_B_OFFSET, \
		[PIPE_C] = CHV_CURSOR_C_OFFSET, \
	}

#define IVB_CURSOR_OFFSETS \
	.display.cursor_offsets = { \
		[PIPE_A] = CURSOR_A_OFFSET, \
		[PIPE_B] = IVB_CURSOR_B_OFFSET, \
		[PIPE_C] = IVB_CURSOR_C_OFFSET, \
	}

#define TGL_CURSOR_OFFSETS \
	.display.cursor_offsets = { \
		[PIPE_A] = CURSOR_A_OFFSET, \
		[PIPE_B] = IVB_CURSOR_B_OFFSET, \
		[PIPE_C] = IVB_CURSOR_C_OFFSET, \
		[PIPE_D] = TGL_CURSOR_D_OFFSET, \
	}

#define I9XX_COLORS \
	.display.color = { .gamma_lut_size = 256 }
#define I965_COLORS \
	.display.color = { .gamma_lut_size = 129, \
		   .gamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
	}
#define ILK_COLORS \
	.display.color = { .gamma_lut_size = 1024 }
#define IVB_COLORS \
	.display.color = { .degamma_lut_size = 1024, .gamma_lut_size = 1024 }
#define CHV_COLORS \
	.display.color = { \
		.degamma_lut_size = 65, .gamma_lut_size = 257, \
		.degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
		.gamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
	}
#define GLK_COLORS \
	.display.color = { \
		.degamma_lut_size = 33, .gamma_lut_size = 1024, \
		.degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING | \
				     DRM_COLOR_LUT_EQUAL_CHANNELS, \
	}
#define ICL_COLORS \
	.display.color = { \
		.degamma_lut_size = 33, .gamma_lut_size = 262145, \
		.degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING | \
				     DRM_COLOR_LUT_EQUAL_CHANNELS, \
		.gamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
	}

/* Keep in gen based order, and chronological order within a gen */

#define GEN_DEFAULT_PAGE_SIZES \
	.__runtime.page_sizes = I915_GTT_PAGE_SIZE_4K

#define GEN_DEFAULT_REGIONS \
	.__runtime.memory_regions = REGION_SMEM | REGION_STOLEN_SMEM

#define I830_FEATURES \
	GEN(2), \
	.is_mobile = 1, \
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.display.has_overlay = 1, \
	.display.cursor_needs_physical = 1, \
	.display.overlay_needs_physical = 1, \
	.display.has_gmch = 1, \
	.gpu_reset_clobbers_display = true, \
	.has_3d_pipeline = 1, \
	.hws_needs_physical = 1, \
	.unfenced_needs_alignment = 1, \
	.__runtime.platform_engine_mask = BIT(RCS0), \
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
	.__runtime.pipe_mask = BIT(PIPE_A), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A), \
	.display.has_overlay = 1, \
	.display.overlay_needs_physical = 1, \
	.display.has_gmch = 1, \
	.has_3d_pipeline = 1, \
	.gpu_reset_clobbers_display = true, \
	.hws_needs_physical = 1, \
	.unfenced_needs_alignment = 1, \
	.__runtime.platform_engine_mask = BIT(RCS0), \
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
	.__runtime.fbc_mask = BIT(INTEL_FBC_A),
};

static const struct intel_device_info i865g_info = {
	I845_FEATURES,
	PLATFORM(INTEL_I865G),
	.__runtime.fbc_mask = BIT(INTEL_FBC_A),
};

#define GEN3_FEATURES \
	GEN(3), \
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.display.has_gmch = 1, \
	.gpu_reset_clobbers_display = true, \
	.__runtime.platform_engine_mask = BIT(RCS0), \
	.has_3d_pipeline = 1, \
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
	.__runtime.fbc_mask = BIT(INTEL_FBC_A),
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
	.__runtime.fbc_mask = BIT(INTEL_FBC_A),
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
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.display.has_hotplug = 1, \
	.display.has_gmch = 1, \
	.gpu_reset_clobbers_display = true, \
	.__runtime.platform_engine_mask = BIT(RCS0), \
	.has_3d_pipeline = 1, \
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
	.__runtime.fbc_mask = BIT(INTEL_FBC_A),
	.display.has_overlay = 1,
	.display.supports_tv = 1,
	.hws_needs_physical = 1,
	.has_snoop = false,
};

static const struct intel_device_info g45_info = {
	GEN4_FEATURES,
	PLATFORM(INTEL_G45),
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(VCS0),
	.gpu_reset_clobbers_display = false,
};

static const struct intel_device_info gm45_info = {
	GEN4_FEATURES,
	PLATFORM(INTEL_GM45),
	.is_mobile = 1,
	.__runtime.fbc_mask = BIT(INTEL_FBC_A),
	.display.supports_tv = 1,
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(VCS0),
	.gpu_reset_clobbers_display = false,
};

#define GEN5_FEATURES \
	GEN(5), \
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.display.has_hotplug = 1, \
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(VCS0), \
	.has_3d_pipeline = 1, \
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
	.__runtime.fbc_mask = BIT(INTEL_FBC_A),
};

#define GEN6_FEATURES \
	GEN(6), \
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.display.has_hotplug = 1, \
	.__runtime.fbc_mask = BIT(INTEL_FBC_A), \
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0), \
	.has_3d_pipeline = 1, \
	.has_coherent_ggtt = true, \
	.has_llc = 1, \
	.has_rc6 = 1, \
	/* snb does support rc6p, but enabling it causes various issues */ \
	.has_rc6p = 0, \
	.has_rps = true, \
	.dma_mask_size = 40, \
	.__runtime.ppgtt_type = INTEL_PPGTT_ALIASING, \
	.__runtime.ppgtt_size = 31, \
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
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | BIT(TRANSCODER_C), \
	.display.has_hotplug = 1, \
	.__runtime.fbc_mask = BIT(INTEL_FBC_A), \
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0), \
	.has_3d_pipeline = 1, \
	.has_coherent_ggtt = true, \
	.has_llc = 1, \
	.has_rc6 = 1, \
	.has_rc6p = 1, \
	.has_reset_engine = true, \
	.has_rps = true, \
	.dma_mask_size = 40, \
	.__runtime.ppgtt_type = INTEL_PPGTT_ALIASING, \
	.__runtime.ppgtt_size = 31, \
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
	NO_DISPLAY,
	.gt = 2,
	.has_l3_dpf = 1,
};

static const struct intel_device_info vlv_info = {
	PLATFORM(INTEL_VALLEYVIEW),
	GEN(7),
	.is_lp = 1,
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B),
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B),
	.has_runtime_pm = 1,
	.has_rc6 = 1,
	.has_reset_engine = true,
	.has_rps = true,
	.display.has_gmch = 1,
	.display.has_hotplug = 1,
	.dma_mask_size = 40,
	.__runtime.ppgtt_type = INTEL_PPGTT_ALIASING,
	.__runtime.ppgtt_size = 31,
	.has_snoop = true,
	.has_coherent_ggtt = false,
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0),
	.display.mmio_offset = VLV_DISPLAY_BASE,
	I9XX_PIPE_OFFSETS,
	I9XX_CURSOR_OFFSETS,
	I965_COLORS,
	GEN_DEFAULT_PAGE_SIZES,
	GEN_DEFAULT_REGIONS,
};

#define G75_FEATURES  \
	GEN7_FEATURES, \
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP), \
	.display.has_ddi = 1, \
	.display.has_fpga_dbg = 1, \
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
	.__runtime.ppgtt_type = INTEL_PPGTT_FULL, \
	.__runtime.ppgtt_size = 48, \
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
	.__runtime.platform_engine_mask =
		BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS1),
};

static const struct intel_device_info chv_info = {
	PLATFORM(INTEL_CHERRYVIEW),
	GEN(8),
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | BIT(TRANSCODER_C),
	.display.has_hotplug = 1,
	.is_lp = 1,
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0),
	.has_64bit_reloc = 1,
	.has_runtime_pm = 1,
	.has_rc6 = 1,
	.has_rps = true,
	.has_logical_ring_contexts = 1,
	.display.has_gmch = 1,
	.dma_mask_size = 39,
	.__runtime.ppgtt_type = INTEL_PPGTT_FULL,
	.__runtime.ppgtt_size = 32,
	.has_reset_engine = 1,
	.has_snoop = true,
	.has_coherent_ggtt = false,
	.display.mmio_offset = VLV_DISPLAY_BASE,
	CHV_PIPE_OFFSETS,
	CHV_CURSOR_OFFSETS,
	CHV_COLORS,
	GEN_DEFAULT_PAGE_SIZES,
	GEN_DEFAULT_REGIONS,
};

#define GEN9_DEFAULT_PAGE_SIZES \
	.__runtime.page_sizes = I915_GTT_PAGE_SIZE_4K | \
		I915_GTT_PAGE_SIZE_64K

#define GEN9_FEATURES \
	GEN8_FEATURES, \
	GEN(9), \
	GEN9_DEFAULT_PAGE_SIZES, \
	.__runtime.has_dmc = 1, \
	.has_gt_uc = 1, \
	.__runtime.has_hdcp = 1, \
	.display.has_ipc = 1, \
	.display.has_psr = 1, \
	.display.has_psr_hw_tracking = 1, \
	.display.dbuf.size = 896 - 4, /* 4 blocks for bypass path allocation */ \
	.display.dbuf.slice_mask = BIT(DBUF_S1)

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
	.__runtime.platform_engine_mask = \
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
	.display.dbuf.slice_mask = BIT(DBUF_S1), \
	.display.has_hotplug = 1, \
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(VCS0) | BIT(BCS0) | BIT(VECS0), \
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP) | \
		BIT(TRANSCODER_DSI_A) | BIT(TRANSCODER_DSI_C), \
	.has_3d_pipeline = 1, \
	.has_64bit_reloc = 1, \
	.display.has_ddi = 1, \
	.display.has_fpga_dbg = 1, \
	.__runtime.fbc_mask = BIT(INTEL_FBC_A), \
	.__runtime.has_hdcp = 1, \
	.display.has_psr = 1, \
	.display.has_psr_hw_tracking = 1, \
	.has_runtime_pm = 1, \
	.__runtime.has_dmc = 1, \
	.has_rc6 = 1, \
	.has_rps = true, \
	.display.has_dp_mst = 1, \
	.has_logical_ring_contexts = 1, \
	.has_gt_uc = 1, \
	.dma_mask_size = 39, \
	.__runtime.ppgtt_type = INTEL_PPGTT_FULL, \
	.__runtime.ppgtt_size = 48, \
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
	.display.dbuf.size = 512 - 4, /* 4 blocks for bypass path allocation */
};

static const struct intel_device_info glk_info = {
	GEN9_LP_FEATURES,
	PLATFORM(INTEL_GEMINILAKE),
	.__runtime.display.ip.ver = 10,
	.display.dbuf.size = 1024 - 4, /* 4 blocks for bypass path allocation */
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
	.__runtime.platform_engine_mask =
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
	.__runtime.platform_engine_mask =
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

#define GEN11_DEFAULT_PAGE_SIZES \
	.__runtime.page_sizes = I915_GTT_PAGE_SIZE_4K | \
		I915_GTT_PAGE_SIZE_64K |		\
		I915_GTT_PAGE_SIZE_2M

#define GEN11_FEATURES \
	GEN9_FEATURES, \
	GEN11_DEFAULT_PAGE_SIZES, \
	.display.abox_mask = BIT(0), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP) | \
		BIT(TRANSCODER_DSI_0) | BIT(TRANSCODER_DSI_1), \
	.display.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET, \
		[TRANSCODER_B] = PIPE_B_OFFSET, \
		[TRANSCODER_C] = PIPE_C_OFFSET, \
		[TRANSCODER_EDP] = PIPE_EDP_OFFSET, \
		[TRANSCODER_DSI_0] = PIPE_DSI0_OFFSET, \
		[TRANSCODER_DSI_1] = PIPE_DSI1_OFFSET, \
	}, \
	.display.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
		[TRANSCODER_C] = TRANSCODER_C_OFFSET, \
		[TRANSCODER_EDP] = TRANSCODER_EDP_OFFSET, \
		[TRANSCODER_DSI_0] = TRANSCODER_DSI0_OFFSET, \
		[TRANSCODER_DSI_1] = TRANSCODER_DSI1_OFFSET, \
	}, \
	GEN(11), \
	ICL_COLORS, \
	.display.dbuf.size = 2048, \
	.display.dbuf.slice_mask = BIT(DBUF_S1) | BIT(DBUF_S2), \
	.__runtime.has_dsc = 1, \
	.has_coherent_ggtt = false, \
	.has_logical_ring_elsq = 1

static const struct intel_device_info icl_info = {
	GEN11_FEATURES,
	PLATFORM(INTEL_ICELAKE),
	.__runtime.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS0) | BIT(VCS2),
};

static const struct intel_device_info ehl_info = {
	GEN11_FEATURES,
	PLATFORM(INTEL_ELKHARTLAKE),
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(BCS0) | BIT(VCS0) | BIT(VECS0),
	.__runtime.ppgtt_size = 36,
};

static const struct intel_device_info jsl_info = {
	GEN11_FEATURES,
	PLATFORM(INTEL_JASPERLAKE),
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(BCS0) | BIT(VCS0) | BIT(VECS0),
	.__runtime.ppgtt_size = 36,
};

#define GEN12_FEATURES \
	GEN11_FEATURES, \
	GEN(12), \
	.display.abox_mask = GENMASK(2, 1), \
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D), \
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D) | \
		BIT(TRANSCODER_DSI_0) | BIT(TRANSCODER_DSI_1), \
	.display.pipe_offsets = { \
		[TRANSCODER_A] = PIPE_A_OFFSET, \
		[TRANSCODER_B] = PIPE_B_OFFSET, \
		[TRANSCODER_C] = PIPE_C_OFFSET, \
		[TRANSCODER_D] = PIPE_D_OFFSET, \
		[TRANSCODER_DSI_0] = PIPE_DSI0_OFFSET, \
		[TRANSCODER_DSI_1] = PIPE_DSI1_OFFSET, \
	}, \
	.display.trans_offsets = { \
		[TRANSCODER_A] = TRANSCODER_A_OFFSET, \
		[TRANSCODER_B] = TRANSCODER_B_OFFSET, \
		[TRANSCODER_C] = TRANSCODER_C_OFFSET, \
		[TRANSCODER_D] = TRANSCODER_D_OFFSET, \
		[TRANSCODER_DSI_0] = TRANSCODER_DSI0_OFFSET, \
		[TRANSCODER_DSI_1] = TRANSCODER_DSI1_OFFSET, \
	}, \
	TGL_CURSOR_OFFSETS, \
	.has_global_mocs = 1, \
	.has_pxp = 1, \
	.display.has_dsb = 0 /* FIXME: LUT load is broken with DSB */

static const struct intel_device_info tgl_info = {
	GEN12_FEATURES,
	PLATFORM(INTEL_TIGERLAKE),
	.display.has_modular_fia = 1,
	.__runtime.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS0) | BIT(VCS2),
};

static const struct intel_device_info rkl_info = {
	GEN12_FEATURES,
	PLATFORM(INTEL_ROCKETLAKE),
	.display.abox_mask = BIT(0),
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C),
	.display.has_hti = 1,
	.display.has_psr_hw_tracking = 0,
	.__runtime.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS0),
};

#define DGFX_FEATURES \
	.__runtime.memory_regions = REGION_SMEM | REGION_LMEM | REGION_STOLEN_LMEM, \
	.has_llc = 0, \
	.has_pxp = 0, \
	.has_snoop = 1, \
	.is_dgfx = 1, \
	.has_heci_gscfi = 1

static const struct intel_device_info dg1_info = {
	GEN12_FEATURES,
	DGFX_FEATURES,
	.__runtime.graphics.ip.rel = 10,
	PLATFORM(INTEL_DG1),
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D),
	.require_force_probe = 1,
	.__runtime.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) | BIT(VECS0) |
		BIT(VCS0) | BIT(VCS2),
	/* Wa_16011227922 */
	.__runtime.ppgtt_size = 47,
};

static const struct intel_device_info adl_s_info = {
	GEN12_FEATURES,
	PLATFORM(INTEL_ALDERLAKE_S),
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D),
	.display.has_hti = 1,
	.display.has_psr_hw_tracking = 0,
	.__runtime.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS0) | BIT(VCS2),
	.dma_mask_size = 39,
};

#define XE_LPD_FEATURES \
	.display.abox_mask = GENMASK(1, 0),					\
	.display.color = {							\
		.degamma_lut_size = 128, .gamma_lut_size = 1024,		\
		.degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING |		\
				     DRM_COLOR_LUT_EQUAL_CHANNELS,		\
	},									\
	.display.dbuf.size = 4096,						\
	.display.dbuf.slice_mask = BIT(DBUF_S1) | BIT(DBUF_S2) | BIT(DBUF_S3) |	\
		BIT(DBUF_S4),							\
	.display.has_ddi = 1,							\
	.__runtime.has_dmc = 1,							\
	.display.has_dp_mst = 1,						\
	.display.has_dsb = 1,							\
	.__runtime.has_dsc = 1,							\
	.__runtime.fbc_mask = BIT(INTEL_FBC_A),					\
	.display.has_fpga_dbg = 1,						\
	.__runtime.has_hdcp = 1,						\
	.display.has_hotplug = 1,						\
	.display.has_ipc = 1,							\
	.display.has_psr = 1,							\
	.__runtime.display.ip.ver = 13,							\
	.__runtime.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D),	\
	.display.pipe_offsets = {						\
		[TRANSCODER_A] = PIPE_A_OFFSET,					\
		[TRANSCODER_B] = PIPE_B_OFFSET,					\
		[TRANSCODER_C] = PIPE_C_OFFSET,					\
		[TRANSCODER_D] = PIPE_D_OFFSET,					\
		[TRANSCODER_DSI_0] = PIPE_DSI0_OFFSET,				\
		[TRANSCODER_DSI_1] = PIPE_DSI1_OFFSET,				\
	},									\
	.display.trans_offsets = {						\
		[TRANSCODER_A] = TRANSCODER_A_OFFSET,				\
		[TRANSCODER_B] = TRANSCODER_B_OFFSET,				\
		[TRANSCODER_C] = TRANSCODER_C_OFFSET,				\
		[TRANSCODER_D] = TRANSCODER_D_OFFSET,				\
		[TRANSCODER_DSI_0] = TRANSCODER_DSI0_OFFSET,			\
		[TRANSCODER_DSI_1] = TRANSCODER_DSI1_OFFSET,			\
	},									\
	TGL_CURSOR_OFFSETS

static const struct intel_device_info adl_p_info = {
	GEN12_FEATURES,
	XE_LPD_FEATURES,
	PLATFORM(INTEL_ALDERLAKE_P),
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
			       BIT(TRANSCODER_C) | BIT(TRANSCODER_D) |
			       BIT(TRANSCODER_DSI_0) | BIT(TRANSCODER_DSI_1),
	.display.has_cdclk_crawl = 1,
	.display.has_modular_fia = 1,
	.display.has_psr_hw_tracking = 0,
	.__runtime.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) | BIT(VECS0) | BIT(VCS0) | BIT(VCS2),
	.__runtime.ppgtt_size = 48,
	.dma_mask_size = 39,
};

#undef GEN

#define XE_HP_PAGE_SIZES \
	.__runtime.page_sizes = I915_GTT_PAGE_SIZE_4K | \
		I915_GTT_PAGE_SIZE_64K |		\
		I915_GTT_PAGE_SIZE_2M

#define XE_HP_FEATURES \
	.__runtime.graphics.ip.ver = 12, \
	.__runtime.graphics.ip.rel = 50, \
	XE_HP_PAGE_SIZES, \
	.dma_mask_size = 46, \
	.has_3d_pipeline = 1, \
	.has_64bit_reloc = 1, \
	.has_flat_ccs = 1, \
	.has_global_mocs = 1, \
	.has_gt_uc = 1, \
	.has_llc = 1, \
	.has_logical_ring_contexts = 1, \
	.has_logical_ring_elsq = 1, \
	.has_mslice_steering = 1, \
	.has_rc6 = 1, \
	.has_reset_engine = 1, \
	.has_rps = 1, \
	.has_runtime_pm = 1, \
	.__runtime.ppgtt_size = 48, \
	.__runtime.ppgtt_type = INTEL_PPGTT_FULL

#define XE_HPM_FEATURES \
	.__runtime.media.ip.ver = 12, \
	.__runtime.media.ip.rel = 50

__maybe_unused
static const struct intel_device_info xehpsdv_info = {
	XE_HP_FEATURES,
	XE_HPM_FEATURES,
	DGFX_FEATURES,
	PLATFORM(INTEL_XEHPSDV),
	NO_DISPLAY,
	.has_64k_pages = 1,
	.needs_compact_pt = 1,
	.has_media_ratio_mode = 1,
	.__runtime.platform_engine_mask =
		BIT(RCS0) | BIT(BCS0) |
		BIT(VECS0) | BIT(VECS1) | BIT(VECS2) | BIT(VECS3) |
		BIT(VCS0) | BIT(VCS1) | BIT(VCS2) | BIT(VCS3) |
		BIT(VCS4) | BIT(VCS5) | BIT(VCS6) | BIT(VCS7) |
		BIT(CCS0) | BIT(CCS1) | BIT(CCS2) | BIT(CCS3),
	.require_force_probe = 1,
};

#define DG2_FEATURES \
	XE_HP_FEATURES, \
	XE_HPM_FEATURES, \
	DGFX_FEATURES, \
	.__runtime.graphics.ip.rel = 55, \
	.__runtime.media.ip.rel = 55, \
	PLATFORM(INTEL_DG2), \
	.has_4tile = 1, \
	.has_64k_pages = 1, \
	.has_guc_deprivilege = 1, \
	.has_heci_pxp = 1, \
	.needs_compact_pt = 1, \
	.has_media_ratio_mode = 1, \
	.__runtime.platform_engine_mask = \
		BIT(RCS0) | BIT(BCS0) | \
		BIT(VECS0) | BIT(VECS1) | \
		BIT(VCS0) | BIT(VCS2) | \
		BIT(CCS0) | BIT(CCS1) | BIT(CCS2) | BIT(CCS3)

static const struct intel_device_info dg2_info = {
	DG2_FEATURES,
	XE_LPD_FEATURES,
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
			       BIT(TRANSCODER_C) | BIT(TRANSCODER_D),
	.require_force_probe = 1,
};

static const struct intel_device_info ats_m_info = {
	DG2_FEATURES,
	NO_DISPLAY,
	.require_force_probe = 1,
	.tuning_thread_rr_after_dep = 1,
};

#define XE_HPC_FEATURES \
	XE_HP_FEATURES, \
	.dma_mask_size = 52, \
	.has_3d_pipeline = 0, \
	.has_guc_deprivilege = 1, \
	.has_l3_ccs_read = 1, \
	.has_mslice_steering = 0, \
	.has_one_eu_per_fuse_bit = 1

__maybe_unused
static const struct intel_device_info pvc_info = {
	XE_HPC_FEATURES,
	XE_HPM_FEATURES,
	DGFX_FEATURES,
	.__runtime.graphics.ip.rel = 60,
	.__runtime.media.ip.rel = 60,
	PLATFORM(INTEL_PONTEVECCHIO),
	NO_DISPLAY,
	.has_flat_ccs = 0,
	.__runtime.platform_engine_mask =
		BIT(BCS0) |
		BIT(VCS0) |
		BIT(CCS0) | BIT(CCS1) | BIT(CCS2) | BIT(CCS3),
	.require_force_probe = 1,
};

#define XE_LPDP_FEATURES	\
	XE_LPD_FEATURES,	\
	.__runtime.display.ip.ver = 14,	\
	.display.has_cdclk_crawl = 1, \
	.__runtime.fbc_mask = BIT(INTEL_FBC_A) | BIT(INTEL_FBC_B)

static const struct intel_gt_definition xelpmp_extra_gt[] = {
	{
		.type = GT_MEDIA,
		.name = "Standalone Media GT",
		.gsi_offset = MTL_MEDIA_GSI_BASE,
		.engine_mask = BIT(VECS0) | BIT(VCS0) | BIT(VCS2),
	},
	{}
};

static const struct intel_device_info mtl_info = {
	XE_HP_FEATURES,
	XE_LPDP_FEATURES,
	.__runtime.cpu_transcoder_mask = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
			       BIT(TRANSCODER_C) | BIT(TRANSCODER_D),
	/*
	 * Real graphics IP version will be obtained from hardware GMD_ID
	 * register.  Value provided here is just for sanity checking.
	 */
	.__runtime.graphics.ip.ver = 12,
	.__runtime.graphics.ip.rel = 70,
	.__runtime.media.ip.ver = 13,
	PLATFORM(INTEL_METEORLAKE),
	.display.has_modular_fia = 1,
	.extra_gt_list = xelpmp_extra_gt,
	.has_flat_ccs = 0,
	.has_snoop = 1,
	.__runtime.memory_regions = REGION_SMEM | REGION_STOLEN_LMEM,
	.__runtime.platform_engine_mask = BIT(RCS0) | BIT(BCS0) | BIT(CCS0),
	.require_force_probe = 1,
};

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
	INTEL_ICL_11_IDS(&icl_info),
	INTEL_EHL_IDS(&ehl_info),
	INTEL_JSL_IDS(&jsl_info),
	INTEL_TGL_12_IDS(&tgl_info),
	INTEL_RKL_IDS(&rkl_info),
	INTEL_ADLS_IDS(&adl_s_info),
	INTEL_ADLP_IDS(&adl_p_info),
	INTEL_ADLN_IDS(&adl_p_info),
	INTEL_DG1_IDS(&dg1_info),
	INTEL_RPLS_IDS(&adl_s_info),
	INTEL_RPLP_IDS(&adl_p_info),
	INTEL_DG2_IDS(&dg2_info),
	INTEL_ATS_M_IDS(&ats_m_info),
	INTEL_MTL_IDS(&mtl_info),
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
	return device_id_in_list(device_id, i915_modparams.force_probe, false);
}

static bool id_blocked(u16 device_id)
{
	return device_id_in_list(device_id, i915_modparams.force_probe, true);
}

bool i915_pci_resource_valid(struct pci_dev *pdev, int bar)
{
	if (!pci_resource_flags(pdev, bar))
		return false;

	if (pci_resource_flags(pdev, bar) & IORESOURCE_UNSET)
		return false;

	if (!pci_resource_len(pdev, bar))
		return false;

	return true;
}

static bool intel_mmio_bar_valid(struct pci_dev *pdev, struct intel_device_info *intel_info)
{
	int gttmmaddr_bar = intel_info->__runtime.graphics.ip.ver == 2 ? GEN2_GTTMMADR_BAR : GTTMMADR_BAR;

	return i915_pci_resource_valid(pdev, gttmmaddr_bar);
}

static int i915_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct intel_device_info *intel_info =
		(struct intel_device_info *) ent->driver_data;
	int err;

	if (intel_info->require_force_probe && !id_forced(pdev->device)) {
		dev_info(&pdev->dev,
			 "Your graphics device %04x is not properly supported by i915 in this\n"
			 "kernel version. To force driver probe anyway, use i915.force_probe=%04x\n"
			 "module parameter or CONFIG_DRM_I915_FORCE_PROBE=%04x configuration option,\n"
			 "or (recommended) check for kernel updates.\n",
			 pdev->device, pdev->device, pdev->device);
		return -ENODEV;
	}

	if (id_blocked(pdev->device)) {
		dev_info(&pdev->dev, "I915 probe blocked for Device ID %04x.\n",
			 pdev->device);
		return -ENODEV;
	}

	if (intel_info->require_force_probe) {
		dev_info(&pdev->dev, "Force probing unsupported Device ID %04x, tainting kernel\n",
			 pdev->device);
		add_taint(TAINT_USER, LOCKDEP_STILL_OK);
	}

	/* Only bind to function 0 of the device. Early generations
	 * used function 1 as a placeholder for multi-head. This causes
	 * us confusion instead, especially on the systems where both
	 * functions have the same PCI-ID!
	 */
	if (PCI_FUNC(pdev->devfn))
		return -ENODEV;

	if (!intel_mmio_bar_valid(pdev, intel_info))
		return -ENXIO;

	/* Detect if we need to wait for other drivers early on */
	if (intel_modeset_probe_defer(pdev))
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

int i915_pci_register_driver(void)
{
	return pci_register_driver(&i915_pci_driver);
}

void i915_pci_unregister_driver(void)
{
	pci_unregister_driver(&i915_pci_driver);
}

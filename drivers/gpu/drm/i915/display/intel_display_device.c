// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/intel/pciids.h>
#include <drm/drm_color_mgmt.h>
#include <linux/pci.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_cx0_phy_regs.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_device.h"
#include "intel_display_params.h"
#include "intel_display_power.h"
#include "intel_display_reg_defs.h"
#include "intel_display_types.h"
#include "intel_fbc.h"
#include "intel_step.h"

__diag_push();
__diag_ignore_all("-Woverride-init", "Allow field initialization overrides for display info");

struct stepping_desc {
	const enum intel_step *map; /* revid to step map */
	size_t size; /* map size */
};

#define STEP_INFO(_map)				\
	.step_info.map = _map,			\
	.step_info.size = ARRAY_SIZE(_map)

struct subplatform_desc {
	struct intel_display_platforms platforms;
	const char *name;
	const u16 *pciidlist;
	struct stepping_desc step_info;
};

#define SUBPLATFORM(_platform, _subplatform)				\
	.platforms._platform##_##_subplatform = 1,			\
	.name = #_subplatform

/*
 * Group subplatform alias that matches multiple subplatforms. For making ult
 * cover both ult and ulx on HSW/BDW.
 */
#define SUBPLATFORM_GROUP(_platform, _subplatform)			\
	.platforms._platform##_##_subplatform = 1

struct platform_desc {
	struct intel_display_platforms platforms;
	const char *name;
	const struct subplatform_desc *subplatforms;
	const struct intel_display_device_info *info; /* NULL for GMD ID */
	struct stepping_desc step_info;
};

#define PLATFORM(_platform)			 \
	.platforms._platform = 1,		 \
	.name = #_platform

/*
 * Group platform alias that matches multiple platforms. For aliases such as g4x
 * that covers both g45 and gm45.
 */
#define PLATFORM_GROUP(_platform)		\
	.platforms._platform = 1

#define ID(id) (id)

static const struct intel_display_device_info no_display = {};

#define PIPE_A_OFFSET		0x70000
#define PIPE_B_OFFSET		0x71000
#define PIPE_C_OFFSET		0x72000
#define PIPE_D_OFFSET		0x73000
#define CHV_PIPE_C_OFFSET	0x74000
/*
 * There's actually no pipe EDP. Some pipe registers have
 * simply shifted from the pipe to the transcoder, while
 * keeping their original offset. Thus we need PIPE_EDP_OFFSET
 * to access such registers in transcoder EDP.
 */
#define PIPE_EDP_OFFSET	0x7f000

/* ICL DSI 0 and 1 */
#define PIPE_DSI0_OFFSET	0x7b000
#define PIPE_DSI1_OFFSET	0x7b800

#define TRANSCODER_A_OFFSET 0x60000
#define TRANSCODER_B_OFFSET 0x61000
#define TRANSCODER_C_OFFSET 0x62000
#define CHV_TRANSCODER_C_OFFSET 0x63000
#define TRANSCODER_D_OFFSET 0x63000
#define TRANSCODER_EDP_OFFSET 0x6f000
#define TRANSCODER_DSI0_OFFSET	0x6b000
#define TRANSCODER_DSI1_OFFSET	0x6b800

#define CURSOR_A_OFFSET 0x70080
#define CURSOR_B_OFFSET 0x700c0
#define CHV_CURSOR_C_OFFSET 0x700e0
#define IVB_CURSOR_B_OFFSET 0x71080
#define IVB_CURSOR_C_OFFSET 0x72080
#define TGL_CURSOR_D_OFFSET 0x73080

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

#define I845_COLORS \
	.color = { .gamma_lut_size = 256 }
#define I9XX_COLORS \
	.color = { .gamma_lut_size = 129, \
		   .gamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
	}
#define ILK_COLORS \
	.color = { .gamma_lut_size = 1024 }
#define IVB_COLORS \
	.color = { .degamma_lut_size = 1024, .gamma_lut_size = 1024 }
#define CHV_COLORS \
	.color = { \
		.degamma_lut_size = 65, .gamma_lut_size = 257, \
		.degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
		.gamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
	}
#define GLK_COLORS \
	.color = { \
		.degamma_lut_size = 33, .gamma_lut_size = 1024, \
		.degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING | \
				     DRM_COLOR_LUT_EQUAL_CHANNELS, \
	}
#define ICL_COLORS \
	.color = { \
		.degamma_lut_size = 33, .gamma_lut_size = 262145, \
		.degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING | \
				     DRM_COLOR_LUT_EQUAL_CHANNELS, \
		.gamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING, \
	}

#define I830_DISPLAY \
	.has_overlay = 1, \
	.cursor_needs_physical = 1, \
	.overlay_needs_physical = 1, \
	.has_gmch = 1, \
	I9XX_PIPE_OFFSETS, \
	I9XX_CURSOR_OFFSETS, \
	I9XX_COLORS, \
	\
	.__runtime_defaults.ip.ver = 2, \
	.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.__runtime_defaults.cpu_transcoder_mask = \
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B)

#define I845_DISPLAY \
	.has_overlay = 1, \
	.overlay_needs_physical = 1, \
	.has_gmch = 1, \
	I845_PIPE_OFFSETS, \
	I845_CURSOR_OFFSETS, \
	I845_COLORS, \
	\
	.__runtime_defaults.ip.ver = 2, \
	.__runtime_defaults.pipe_mask = BIT(PIPE_A), \
	.__runtime_defaults.cpu_transcoder_mask = BIT(TRANSCODER_A)

static const struct platform_desc i830_desc = {
	PLATFORM(i830),
	PLATFORM_GROUP(mobile),
	.info = &(const struct intel_display_device_info) {
		I830_DISPLAY,

		.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C), /* DVO A/B/C */
	},
};

static const struct platform_desc i845_desc = {
	PLATFORM(i845g),
	.info = &(const struct intel_display_device_info) {
		I845_DISPLAY,

		.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* DVO B/C */
	},
};

static const struct platform_desc i85x_desc = {
	PLATFORM(i85x),
	PLATFORM_GROUP(mobile),
	.info = &(const struct intel_display_device_info) {
		I830_DISPLAY,

		.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* DVO B/C */
		.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
	},
};

static const struct platform_desc i865g_desc = {
	PLATFORM(i865g),
	.info = &(const struct intel_display_device_info) {
		I845_DISPLAY,

		.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* DVO B/C */
		.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
	},
};

#define GEN3_DISPLAY   \
	.has_gmch = 1, \
	.has_overlay = 1, \
	I9XX_PIPE_OFFSETS, \
	I9XX_CURSOR_OFFSETS, \
	\
	.__runtime_defaults.ip.ver = 3, \
	.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.__runtime_defaults.cpu_transcoder_mask = \
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C) /* SDVO B/C */

static const struct platform_desc i915g_desc = {
	PLATFORM(i915g),
	.info = &(const struct intel_display_device_info) {
		GEN3_DISPLAY,
		I845_COLORS,
		.cursor_needs_physical = 1,
		.overlay_needs_physical = 1,
	},
};

static const struct platform_desc i915gm_desc = {
	PLATFORM(i915gm),
	PLATFORM_GROUP(mobile),
	.info = &(const struct intel_display_device_info) {
		GEN3_DISPLAY,
		I9XX_COLORS,
		.cursor_needs_physical = 1,
		.overlay_needs_physical = 1,
		.supports_tv = 1,

		.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
	},
};

static const struct platform_desc i945g_desc = {
	PLATFORM(i945g),
	.info = &(const struct intel_display_device_info) {
		GEN3_DISPLAY,
		I845_COLORS,
		.has_hotplug = 1,
		.cursor_needs_physical = 1,
		.overlay_needs_physical = 1,
	},
};

static const struct platform_desc i945gm_desc = {
	PLATFORM(i915gm),
	PLATFORM_GROUP(mobile),
	.info = &(const struct intel_display_device_info) {
		GEN3_DISPLAY,
		I9XX_COLORS,
		.has_hotplug = 1,
		.cursor_needs_physical = 1,
		.overlay_needs_physical = 1,
		.supports_tv = 1,

		.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
	},
};

static const struct platform_desc g33_desc = {
	PLATFORM(g33),
	.info = &(const struct intel_display_device_info) {
		GEN3_DISPLAY,
		I845_COLORS,
		.has_hotplug = 1,
	},
};

static const struct intel_display_device_info pnv_display = {
	GEN3_DISPLAY,
	I9XX_COLORS,
	.has_hotplug = 1,
};

static const struct platform_desc pnv_g_desc = {
	PLATFORM(pineview),
	.info = &pnv_display,
};

static const struct platform_desc pnv_m_desc = {
	PLATFORM(pineview),
	PLATFORM_GROUP(mobile),
	.info = &pnv_display,
};

#define GEN4_DISPLAY \
	.has_hotplug = 1, \
	.has_gmch = 1, \
	I9XX_PIPE_OFFSETS, \
	I9XX_CURSOR_OFFSETS, \
	I9XX_COLORS, \
	\
	.__runtime_defaults.ip.ver = 4, \
	.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.__runtime_defaults.cpu_transcoder_mask = \
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B)

static const struct platform_desc i965g_desc = {
	PLATFORM(i965g),
	.info = &(const struct intel_display_device_info) {
		GEN4_DISPLAY,
		.has_overlay = 1,

		.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* SDVO B/C */
	},
};

static const struct platform_desc i965gm_desc = {
	PLATFORM(i965gm),
	PLATFORM_GROUP(mobile),
	.info = &(const struct intel_display_device_info) {
		GEN4_DISPLAY,
		.has_overlay = 1,
		.supports_tv = 1,

		.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* SDVO B/C */
		.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
	},
};

static const struct platform_desc g45_desc = {
	PLATFORM(g45),
	PLATFORM_GROUP(g4x),
	.info = &(const struct intel_display_device_info) {
		GEN4_DISPLAY,

		.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D), /* SDVO/HDMI/DP B/C, DP D */
	},
};

static const struct platform_desc gm45_desc = {
	PLATFORM(gm45),
	PLATFORM_GROUP(g4x),
	PLATFORM_GROUP(mobile),
	.info = &(const struct intel_display_device_info) {
		GEN4_DISPLAY,
		.supports_tv = 1,

		.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D), /* SDVO/HDMI/DP B/C, DP D */
		.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
	},
};

#define ILK_DISPLAY \
	.has_hotplug = 1, \
	I9XX_PIPE_OFFSETS, \
	I9XX_CURSOR_OFFSETS, \
	ILK_COLORS, \
	\
	.__runtime_defaults.ip.ver = 5, \
	.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B), \
	.__runtime_defaults.cpu_transcoder_mask = \
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B), \
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D) /* DP A, SDVO/HDMI/DP B, HDMI/DP C/D */

static const struct platform_desc ilk_d_desc = {
	PLATFORM(ironlake),
	.info = &(const struct intel_display_device_info) {
		ILK_DISPLAY,
	},
};

static const struct platform_desc ilk_m_desc = {
	PLATFORM(ironlake),
	PLATFORM_GROUP(mobile),
	.info = &(const struct intel_display_device_info) {
		ILK_DISPLAY,

		.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
	},
};

static const struct intel_display_device_info snb_display = {
	.has_hotplug = 1,
	I9XX_PIPE_OFFSETS,
	I9XX_CURSOR_OFFSETS,
	ILK_COLORS,

	.__runtime_defaults.ip.ver = 6,
	.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B),
	.__runtime_defaults.cpu_transcoder_mask =
	BIT(TRANSCODER_A) | BIT(TRANSCODER_B),
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D), /* DP A, SDVO/HDMI/DP B, HDMI/DP C/D */
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
};

static const struct platform_desc snb_d_desc = {
	PLATFORM(sandybridge),
	.info = &snb_display,
};

static const struct platform_desc snb_m_desc = {
	PLATFORM(sandybridge),
	PLATFORM_GROUP(mobile),
	.info = &snb_display,
};

static const struct intel_display_device_info ivb_display = {
	.has_hotplug = 1,
	IVB_PIPE_OFFSETS,
	IVB_CURSOR_OFFSETS,
	IVB_COLORS,

	.__runtime_defaults.ip.ver = 7,
	.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
	.__runtime_defaults.cpu_transcoder_mask =
	BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | BIT(TRANSCODER_C),
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D), /* DP A, SDVO/HDMI/DP B, HDMI/DP C/D */
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
};

static const struct platform_desc ivb_d_desc = {
	PLATFORM(ivybridge),
	.info = &ivb_display,
};

static const struct platform_desc ivb_m_desc = {
	PLATFORM(ivybridge),
	PLATFORM_GROUP(mobile),
	.info = &ivb_display,
};

static const struct platform_desc vlv_desc = {
	PLATFORM(valleyview),
	.info = &(const struct intel_display_device_info) {
		.has_gmch = 1,
		.has_hotplug = 1,
		.mmio_offset = VLV_DISPLAY_BASE,
		I9XX_PIPE_OFFSETS,
		I9XX_CURSOR_OFFSETS,
		I9XX_COLORS,

		.__runtime_defaults.ip.ver = 7,
		.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B),
		.__runtime_defaults.cpu_transcoder_mask =
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B),
		.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* HDMI/DP B/C */
	},
};

static const u16 hsw_ult_ids[] = {
	INTEL_HSW_ULT_GT1_IDS(ID),
	INTEL_HSW_ULT_GT2_IDS(ID),
	INTEL_HSW_ULT_GT3_IDS(ID),
	0
};

static const u16 hsw_ulx_ids[] = {
	INTEL_HSW_ULX_GT1_IDS(ID),
	INTEL_HSW_ULX_GT2_IDS(ID),
	0
};

static const struct platform_desc hsw_desc = {
	PLATFORM(haswell),
	.subplatforms = (const struct subplatform_desc[]) {
		/* Special case: Use ult both as group and subplatform. */
		{
			SUBPLATFORM(haswell, ult),
			SUBPLATFORM_GROUP(haswell, ult),
			.pciidlist = hsw_ult_ids,
		},
		{
			SUBPLATFORM(haswell, ulx),
			SUBPLATFORM_GROUP(haswell, ult),
			.pciidlist = hsw_ulx_ids,
		},
		{},
	},
	.info = &(const struct intel_display_device_info) {
		.has_ddi = 1,
		.has_dp_mst = 1,
		.has_fpga_dbg = 1,
		.has_hotplug = 1,
		.has_psr = 1,
		.has_psr_hw_tracking = 1,
		HSW_PIPE_OFFSETS,
		IVB_CURSOR_OFFSETS,
		IVB_COLORS,

		.__runtime_defaults.ip.ver = 7,
		.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
		.__runtime_defaults.cpu_transcoder_mask =
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP),
		.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D) | BIT(PORT_E),
		.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
	},
};

static const u16 bdw_ult_ids[] = {
	INTEL_BDW_ULT_GT1_IDS(ID),
	INTEL_BDW_ULT_GT2_IDS(ID),
	INTEL_BDW_ULT_GT3_IDS(ID),
	INTEL_BDW_ULT_RSVD_IDS(ID),
	0
};

static const u16 bdw_ulx_ids[] = {
	INTEL_BDW_ULX_GT1_IDS(ID),
	INTEL_BDW_ULX_GT2_IDS(ID),
	INTEL_BDW_ULX_GT3_IDS(ID),
	INTEL_BDW_ULX_RSVD_IDS(ID),
	0
};

static const struct platform_desc bdw_desc = {
	PLATFORM(broadwell),
	.subplatforms = (const struct subplatform_desc[]) {
		/* Special case: Use ult both as group and subplatform. */
		{
			SUBPLATFORM(broadwell, ult),
			SUBPLATFORM_GROUP(broadwell, ult),
			.pciidlist = bdw_ult_ids,
		},
		{
			SUBPLATFORM(broadwell, ulx),
			SUBPLATFORM_GROUP(broadwell, ult),
			.pciidlist = bdw_ulx_ids,
		},
		{},
	},
	.info = &(const struct intel_display_device_info) {
		.has_ddi = 1,
		.has_dp_mst = 1,
		.has_fpga_dbg = 1,
		.has_hotplug = 1,
		.has_psr = 1,
		.has_psr_hw_tracking = 1,
		HSW_PIPE_OFFSETS,
		IVB_CURSOR_OFFSETS,
		IVB_COLORS,

		.__runtime_defaults.ip.ver = 8,
		.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
		.__runtime_defaults.cpu_transcoder_mask =
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP),
		.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D) | BIT(PORT_E),
		.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
	},
};

static const struct platform_desc chv_desc = {
	PLATFORM(cherryview),
	.info = &(const struct intel_display_device_info) {
		.has_hotplug = 1,
		.has_gmch = 1,
		.mmio_offset = VLV_DISPLAY_BASE,
		CHV_PIPE_OFFSETS,
		CHV_CURSOR_OFFSETS,
		CHV_COLORS,

		.__runtime_defaults.ip.ver = 8,
		.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
		.__runtime_defaults.cpu_transcoder_mask =
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | BIT(TRANSCODER_C),
		.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D), /* HDMI/DP B/C/D */
	},
};

static const struct intel_display_device_info skl_display = {
	.dbuf.size = 896 - 4, /* 4 blocks for bypass path allocation */
	.dbuf.slice_mask = BIT(DBUF_S1),
	.has_ddi = 1,
	.has_dp_mst = 1,
	.has_fpga_dbg = 1,
	.has_hotplug = 1,
	.has_ipc = 1,
	.has_psr = 1,
	.has_psr_hw_tracking = 1,
	HSW_PIPE_OFFSETS,
	IVB_CURSOR_OFFSETS,
	IVB_COLORS,

	.__runtime_defaults.ip.ver = 9,
	.__runtime_defaults.has_dmc = 1,
	.__runtime_defaults.has_hdcp = 1,
	.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
	.__runtime_defaults.cpu_transcoder_mask =
	BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
	BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP),
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D) | BIT(PORT_E),
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
};

static const u16 skl_ult_ids[] = {
	INTEL_SKL_ULT_GT1_IDS(ID),
	INTEL_SKL_ULT_GT2_IDS(ID),
	INTEL_SKL_ULT_GT3_IDS(ID),
	0
};

static const u16 skl_ulx_ids[] = {
	INTEL_SKL_ULX_GT1_IDS(ID),
	INTEL_SKL_ULX_GT2_IDS(ID),
	0
};

static const enum intel_step skl_steppings[] = {
	[0x6] = STEP_G0,
	[0x7] = STEP_H0,
	[0x9] = STEP_J0,
	[0xA] = STEP_I1,
};

static const struct platform_desc skl_desc = {
	PLATFORM(skylake),
	.subplatforms = (const struct subplatform_desc[]) {
		{
			SUBPLATFORM(skylake, ult),
			.pciidlist = skl_ult_ids,
		},
		{
			SUBPLATFORM(skylake, ulx),
			.pciidlist = skl_ulx_ids,
		},
		{},
	},
	.info = &skl_display,
	STEP_INFO(skl_steppings),
};

static const u16 kbl_ult_ids[] = {
	INTEL_KBL_ULT_GT1_IDS(ID),
	INTEL_KBL_ULT_GT2_IDS(ID),
	INTEL_KBL_ULT_GT3_IDS(ID),
	0
};

static const u16 kbl_ulx_ids[] = {
	INTEL_KBL_ULX_GT1_IDS(ID),
	INTEL_KBL_ULX_GT2_IDS(ID),
	INTEL_AML_KBL_GT2_IDS(ID),
	0
};

static const enum intel_step kbl_steppings[] = {
	[1] = STEP_B0,
	[2] = STEP_B0,
	[3] = STEP_B0,
	[4] = STEP_C0,
	[5] = STEP_B1,
	[6] = STEP_B1,
	[7] = STEP_C0,
};

static const struct platform_desc kbl_desc = {
	PLATFORM(kabylake),
	.subplatforms = (const struct subplatform_desc[]) {
		{
			SUBPLATFORM(kabylake, ult),
			.pciidlist = kbl_ult_ids,
		},
		{
			SUBPLATFORM(kabylake, ulx),
			.pciidlist = kbl_ulx_ids,
		},
		{},
	},
	.info = &skl_display,
	STEP_INFO(kbl_steppings),
};

static const u16 cfl_ult_ids[] = {
	INTEL_CFL_U_GT2_IDS(ID),
	INTEL_CFL_U_GT3_IDS(ID),
	INTEL_WHL_U_GT1_IDS(ID),
	INTEL_WHL_U_GT2_IDS(ID),
	INTEL_WHL_U_GT3_IDS(ID),
	0
};

static const u16 cfl_ulx_ids[] = {
	INTEL_AML_CFL_GT2_IDS(ID),
	0
};

static const struct platform_desc cfl_desc = {
	PLATFORM(coffeelake),
	.subplatforms = (const struct subplatform_desc[]) {
		{
			SUBPLATFORM(coffeelake, ult),
			.pciidlist = cfl_ult_ids,
		},
		{
			SUBPLATFORM(coffeelake, ulx),
			.pciidlist = cfl_ulx_ids,
		},
		{},
	},
	.info = &skl_display,
};

static const u16 cml_ult_ids[] = {
	INTEL_CML_U_GT1_IDS(ID),
	INTEL_CML_U_GT2_IDS(ID),
	0
};

static const struct platform_desc cml_desc = {
	PLATFORM(cometlake),
	.subplatforms = (const struct subplatform_desc[]) {
		{
			SUBPLATFORM(cometlake, ult),
			.pciidlist = cml_ult_ids,
		},
		{},
	},
	.info = &skl_display,
};

#define GEN9_LP_DISPLAY			 \
	.dbuf.slice_mask = BIT(DBUF_S1), \
	.has_dp_mst = 1, \
	.has_ddi = 1, \
	.has_fpga_dbg = 1, \
	.has_hotplug = 1, \
	.has_ipc = 1, \
	.has_psr = 1, \
	.has_psr_hw_tracking = 1, \
	HSW_PIPE_OFFSETS, \
	IVB_CURSOR_OFFSETS, \
	IVB_COLORS, \
	\
	.__runtime_defaults.has_dmc = 1, \
	.__runtime_defaults.has_hdcp = 1, \
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A), \
	.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C), \
	.__runtime_defaults.cpu_transcoder_mask = \
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP) | \
		BIT(TRANSCODER_DSI_A) | BIT(TRANSCODER_DSI_C), \
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C)

static const enum intel_step bxt_steppings[] = {
	[0xA] = STEP_C0,
	[0xB] = STEP_C0,
	[0xC] = STEP_D0,
	[0xD] = STEP_E0,
};

static const struct platform_desc bxt_desc = {
	PLATFORM(broxton),
	.info = &(const struct intel_display_device_info) {
		GEN9_LP_DISPLAY,
		.dbuf.size = 512 - 4, /* 4 blocks for bypass path allocation */

		.__runtime_defaults.ip.ver = 9,
	},
	STEP_INFO(bxt_steppings),
};

static const enum intel_step glk_steppings[] = {
	[3] = STEP_B0,
};

static const struct platform_desc glk_desc = {
	PLATFORM(geminilake),
	.info = &(const struct intel_display_device_info) {
		GEN9_LP_DISPLAY,
		.dbuf.size = 1024 - 4, /* 4 blocks for bypass path allocation */
		GLK_COLORS,

		.__runtime_defaults.ip.ver = 10,
	},
	STEP_INFO(glk_steppings),
};

#define ICL_DISPLAY \
	.abox_mask = BIT(0), \
	.dbuf.size = 2048, \
	.dbuf.slice_mask = BIT(DBUF_S1) | BIT(DBUF_S2), \
	.has_ddi = 1, \
	.has_dp_mst = 1, \
	.has_fpga_dbg = 1, \
	.has_hotplug = 1, \
	.has_ipc = 1, \
	.has_psr = 1, \
	.has_psr_hw_tracking = 1, \
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
	IVB_CURSOR_OFFSETS, \
	ICL_COLORS, \
	\
	.__runtime_defaults.ip.ver = 11, \
	.__runtime_defaults.has_dmc = 1, \
	.__runtime_defaults.has_dsc = 1, \
	.__runtime_defaults.has_hdcp = 1, \
	.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C), \
	.__runtime_defaults.cpu_transcoder_mask = \
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_EDP) | \
		BIT(TRANSCODER_DSI_0) | BIT(TRANSCODER_DSI_1), \
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A)

static const u16 icl_port_f_ids[] = {
	INTEL_ICL_PORT_F_IDS(ID),
	0
};

static const enum intel_step icl_steppings[] = {
	[7] = STEP_D0,
};

static const struct platform_desc icl_desc = {
	PLATFORM(icelake),
	.subplatforms = (const struct subplatform_desc[]) {
		{
			SUBPLATFORM(icelake, port_f),
			.pciidlist = icl_port_f_ids,
		},
		{},
	},
	.info = &(const struct intel_display_device_info) {
		ICL_DISPLAY,

		.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D) | BIT(PORT_E),
	},
	STEP_INFO(icl_steppings),
};

static const struct intel_display_device_info jsl_ehl_display = {
	ICL_DISPLAY,

	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D),
};

static const enum intel_step jsl_ehl_steppings[] = {
	[0] = STEP_A0,
	[1] = STEP_B0,
};

static const struct platform_desc jsl_desc = {
	PLATFORM(jasperlake),
	.info = &jsl_ehl_display,
	STEP_INFO(jsl_ehl_steppings),
};

static const struct platform_desc ehl_desc = {
	PLATFORM(elkhartlake),
	.info = &jsl_ehl_display,
	STEP_INFO(jsl_ehl_steppings),
};

#define XE_D_DISPLAY \
	.abox_mask = GENMASK(2, 1), \
	.dbuf.size = 2048, \
	.dbuf.slice_mask = BIT(DBUF_S1) | BIT(DBUF_S2), \
	.has_ddi = 1, \
	.has_dp_mst = 1, \
	.has_dsb = 1, \
	.has_fpga_dbg = 1, \
	.has_hotplug = 1, \
	.has_ipc = 1, \
	.has_psr = 1, \
	.has_psr_hw_tracking = 1, \
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
	ICL_COLORS, \
	\
	.__runtime_defaults.ip.ver = 12, \
	.__runtime_defaults.has_dmc = 1, \
	.__runtime_defaults.has_dsc = 1, \
	.__runtime_defaults.has_hdcp = 1, \
	.__runtime_defaults.pipe_mask = \
		BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D), \
	.__runtime_defaults.cpu_transcoder_mask = \
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | \
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D) | \
		BIT(TRANSCODER_DSI_0) | BIT(TRANSCODER_DSI_1), \
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A)

static const u16 tgl_uy_ids[] = {
	INTEL_TGL_GT2_IDS(ID),
	0
};

static const enum intel_step tgl_steppings[] = {
	[0] = STEP_B0,
	[1] = STEP_D0,
};

static const enum intel_step tgl_uy_steppings[] = {
	[0] = STEP_A0,
	[1] = STEP_C0,
	[2] = STEP_C0,
	[3] = STEP_D0,
};

static const struct platform_desc tgl_desc = {
	PLATFORM(tigerlake),
	.subplatforms = (const struct subplatform_desc[]) {
		{
			SUBPLATFORM(tigerlake, uy),
			.pciidlist = tgl_uy_ids,
			STEP_INFO(tgl_uy_steppings),
		},
		{},
	},
	.info = &(const struct intel_display_device_info) {
		XE_D_DISPLAY,

		/*
		 * FIXME DDI C/combo PHY C missing due to combo PHY
		 * code making a mess on SKUs where the PHY is missing.
		 */
		.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) |
		BIT(PORT_TC1) | BIT(PORT_TC2) | BIT(PORT_TC3) | BIT(PORT_TC4) | BIT(PORT_TC5) | BIT(PORT_TC6),
	},
	STEP_INFO(tgl_steppings),
};

static const enum intel_step dg1_steppings[] = {
	[0] = STEP_A0,
	[1] = STEP_B0,
};

static const struct platform_desc dg1_desc = {
	PLATFORM(dg1),
	PLATFORM_GROUP(dgfx),
	.info = &(const struct intel_display_device_info) {
		XE_D_DISPLAY,

		.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) |
		BIT(PORT_TC1) | BIT(PORT_TC2),
	},
	STEP_INFO(dg1_steppings),
};

static const enum intel_step rkl_steppings[] = {
	[0] = STEP_A0,
	[1] = STEP_B0,
	[4] = STEP_C0,
};

static const struct platform_desc rkl_desc = {
	PLATFORM(rocketlake),
	.info = &(const struct intel_display_device_info) {
		XE_D_DISPLAY,
		.abox_mask = BIT(0),
		.has_hti = 1,
		.has_psr_hw_tracking = 0,

		.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
		.__runtime_defaults.cpu_transcoder_mask =
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | BIT(TRANSCODER_C),
		.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) |
		BIT(PORT_TC1) | BIT(PORT_TC2),
	},
	STEP_INFO(rkl_steppings),
};

static const u16 adls_rpls_ids[] = {
	INTEL_RPLS_IDS(ID),
	0
};

static const enum intel_step adl_s_steppings[] = {
	[0x0] = STEP_A0,
	[0x1] = STEP_A2,
	[0x4] = STEP_B0,
	[0x8] = STEP_B0,
	[0xC] = STEP_C0,
};

static const enum intel_step adl_s_rpl_s_steppings[] = {
	[0x4] = STEP_D0,
	[0xC] = STEP_C0,
};

static const struct platform_desc adl_s_desc = {
	PLATFORM(alderlake_s),
	.subplatforms = (const struct subplatform_desc[]) {
		{
			SUBPLATFORM(alderlake_s, raptorlake_s),
			.pciidlist = adls_rpls_ids,
			STEP_INFO(adl_s_rpl_s_steppings),
		},
		{},
	},
	.info = &(const struct intel_display_device_info) {
		XE_D_DISPLAY,
		.has_hti = 1,
		.has_psr_hw_tracking = 0,

		.__runtime_defaults.port_mask = BIT(PORT_A) |
		BIT(PORT_TC1) | BIT(PORT_TC2) | BIT(PORT_TC3) | BIT(PORT_TC4),
	},
	STEP_INFO(adl_s_steppings),
};

#define XE_LPD_FEATURES \
	.abox_mask = GENMASK(1, 0),						\
	.color = {								\
		.degamma_lut_size = 129, .gamma_lut_size = 1024,		\
		.degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING |		\
		DRM_COLOR_LUT_EQUAL_CHANNELS,					\
	},									\
	.dbuf.size = 4096,							\
	.dbuf.slice_mask = BIT(DBUF_S1) | BIT(DBUF_S2) | BIT(DBUF_S3) |		\
		BIT(DBUF_S4),							\
	.has_ddi = 1,								\
	.has_dp_mst = 1,							\
	.has_dsb = 1,								\
	.has_fpga_dbg = 1,							\
	.has_hotplug = 1,							\
	.has_ipc = 1,								\
	.has_psr = 1,								\
	.pipe_offsets = {							\
		[TRANSCODER_A] = PIPE_A_OFFSET,					\
		[TRANSCODER_B] = PIPE_B_OFFSET,					\
		[TRANSCODER_C] = PIPE_C_OFFSET,					\
		[TRANSCODER_D] = PIPE_D_OFFSET,					\
		[TRANSCODER_DSI_0] = PIPE_DSI0_OFFSET,				\
		[TRANSCODER_DSI_1] = PIPE_DSI1_OFFSET,				\
	},									\
	.trans_offsets = {							\
		[TRANSCODER_A] = TRANSCODER_A_OFFSET,				\
		[TRANSCODER_B] = TRANSCODER_B_OFFSET,				\
		[TRANSCODER_C] = TRANSCODER_C_OFFSET,				\
		[TRANSCODER_D] = TRANSCODER_D_OFFSET,				\
		[TRANSCODER_DSI_0] = TRANSCODER_DSI0_OFFSET,			\
		[TRANSCODER_DSI_1] = TRANSCODER_DSI1_OFFSET,			\
	},									\
	TGL_CURSOR_OFFSETS,							\
										\
	.__runtime_defaults.ip.ver = 13,					\
	.__runtime_defaults.has_dmc = 1,					\
	.__runtime_defaults.has_dsc = 1,					\
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),			\
	.__runtime_defaults.has_hdcp = 1,					\
	.__runtime_defaults.pipe_mask =						\
		BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D)

static const struct intel_display_device_info xe_lpd_display = {
	XE_LPD_FEATURES,
	.has_cdclk_crawl = 1,
	.has_psr_hw_tracking = 0,

	.__runtime_defaults.cpu_transcoder_mask =
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D) |
		BIT(TRANSCODER_DSI_0) | BIT(TRANSCODER_DSI_1),
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) |
		BIT(PORT_TC1) | BIT(PORT_TC2) | BIT(PORT_TC3) | BIT(PORT_TC4),
};

static const u16 adlp_adln_ids[] = {
	INTEL_ADLN_IDS(ID),
	0
};

static const u16 adlp_rplu_ids[] = {
	INTEL_RPLU_IDS(ID),
	0
};

static const u16 adlp_rplp_ids[] = {
	INTEL_RPLP_IDS(ID),
	0
};

static const enum intel_step adl_p_steppings[] = {
	[0x0] = STEP_A0,
	[0x4] = STEP_B0,
	[0x8] = STEP_C0,
	[0xC] = STEP_D0,
};

static const enum intel_step adl_p_adl_n_steppings[] = {
	[0x0] = STEP_D0,
};

static const enum intel_step adl_p_rpl_pu_steppings[] = {
	[0x4] = STEP_E0,
};

static const struct platform_desc adl_p_desc = {
	PLATFORM(alderlake_p),
	.subplatforms = (const struct subplatform_desc[]) {
		{
			SUBPLATFORM(alderlake_p, alderlake_n),
			.pciidlist = adlp_adln_ids,
			STEP_INFO(adl_p_adl_n_steppings),
		},
		{
			SUBPLATFORM(alderlake_p, raptorlake_p),
			.pciidlist = adlp_rplp_ids,
			STEP_INFO(adl_p_rpl_pu_steppings),
		},
		{
			SUBPLATFORM(alderlake_p, raptorlake_u),
			.pciidlist = adlp_rplu_ids,
			STEP_INFO(adl_p_rpl_pu_steppings),
		},
		{},
	},
	.info = &xe_lpd_display,
	STEP_INFO(adl_p_steppings),
};

static const struct intel_display_device_info xe_hpd_display = {
	XE_LPD_FEATURES,
	.has_cdclk_squash = 1,

	.__runtime_defaults.cpu_transcoder_mask =
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D),
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D_XELPD) |
		BIT(PORT_TC1),
};

static const u16 dg2_g10_ids[] = {
	INTEL_DG2_G10_IDS(ID),
	0
};

static const u16 dg2_g11_ids[] = {
	INTEL_DG2_G11_IDS(ID),
	0
};

static const u16 dg2_g12_ids[] = {
	INTEL_DG2_G12_IDS(ID),
	0
};

static const enum intel_step dg2_g10_steppings[] = {
	[0x0] = STEP_A0,
	[0x1] = STEP_A0,
	[0x4] = STEP_B0,
	[0x8] = STEP_C0,
};

static const enum intel_step dg2_g11_steppings[] = {
	[0x0] = STEP_B0,
	[0x4] = STEP_C0,
	[0x5] = STEP_C0,
};

static const enum intel_step dg2_g12_steppings[] = {
	[0x0] = STEP_C0,
	[0x1] = STEP_C0,
};

static const struct platform_desc dg2_desc = {
	PLATFORM(dg2),
	PLATFORM_GROUP(dgfx),
	.subplatforms = (const struct subplatform_desc[]) {
		{
			SUBPLATFORM(dg2, g10),
			.pciidlist = dg2_g10_ids,
			STEP_INFO(dg2_g10_steppings),
		},
		{
			SUBPLATFORM(dg2, g11),
			.pciidlist = dg2_g11_ids,
			STEP_INFO(dg2_g11_steppings),
		},
		{
			SUBPLATFORM(dg2, g12),
			.pciidlist = dg2_g12_ids,
			STEP_INFO(dg2_g12_steppings),
		},
		{},
	},
	.info = &xe_hpd_display,
};

#define XE_LPDP_FEATURES							\
	.abox_mask = GENMASK(1, 0),						\
	.color = {								\
		.degamma_lut_size = 129, .gamma_lut_size = 1024,		\
		.degamma_lut_tests = DRM_COLOR_LUT_NON_DECREASING |		\
		DRM_COLOR_LUT_EQUAL_CHANNELS,					\
	},									\
	.dbuf.size = 4096,							\
	.dbuf.slice_mask = BIT(DBUF_S1) | BIT(DBUF_S2) | BIT(DBUF_S3) |		\
		BIT(DBUF_S4),							\
	.has_cdclk_crawl = 1,							\
	.has_cdclk_squash = 1,							\
	.has_ddi = 1,								\
	.has_dp_mst = 1,							\
	.has_dsb = 1,								\
	.has_fpga_dbg = 1,							\
	.has_hotplug = 1,							\
	.has_ipc = 1,								\
	.has_psr = 1,								\
	.pipe_offsets = {							\
		[TRANSCODER_A] = PIPE_A_OFFSET,					\
		[TRANSCODER_B] = PIPE_B_OFFSET,					\
		[TRANSCODER_C] = PIPE_C_OFFSET,					\
		[TRANSCODER_D] = PIPE_D_OFFSET,					\
	},									\
	.trans_offsets = {							\
		[TRANSCODER_A] = TRANSCODER_A_OFFSET,				\
		[TRANSCODER_B] = TRANSCODER_B_OFFSET,				\
		[TRANSCODER_C] = TRANSCODER_C_OFFSET,				\
		[TRANSCODER_D] = TRANSCODER_D_OFFSET,				\
	},									\
	TGL_CURSOR_OFFSETS,							\
										\
	.__runtime_defaults.cpu_transcoder_mask =				\
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |				\
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D),				\
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A) | BIT(INTEL_FBC_B),	\
	.__runtime_defaults.has_dmc = 1,					\
	.__runtime_defaults.has_dsc = 1,					\
	.__runtime_defaults.has_hdcp = 1,					\
	.__runtime_defaults.pipe_mask =						\
		BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D),		\
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) |		\
		BIT(PORT_TC1) | BIT(PORT_TC2) | BIT(PORT_TC3) | BIT(PORT_TC4)

static const struct intel_display_device_info xe_lpdp_display = {
	XE_LPDP_FEATURES,
};

static const struct intel_display_device_info xe2_lpd_display = {
	XE_LPDP_FEATURES,

	.__runtime_defaults.fbc_mask =
		BIT(INTEL_FBC_A) | BIT(INTEL_FBC_B) |
		BIT(INTEL_FBC_C) | BIT(INTEL_FBC_D),
	.__runtime_defaults.has_dbuf_overlap_detection = true,
};

static const struct intel_display_device_info xe2_hpd_display = {
	XE_LPDP_FEATURES,
	.__runtime_defaults.port_mask = BIT(PORT_A) |
		BIT(PORT_TC1) | BIT(PORT_TC2) | BIT(PORT_TC3) | BIT(PORT_TC4),
};

static const u16 mtl_u_ids[] = {
	INTEL_MTL_U_IDS(ID),
	INTEL_ARL_U_IDS(ID),
	0
};

/*
 * Do not initialize the .info member of the platform desc for GMD ID based
 * platforms. Their display will be probed automatically based on the IP version
 * reported by the hardware.
 */
static const struct platform_desc mtl_desc = {
	PLATFORM(meteorlake),
	.subplatforms = (const struct subplatform_desc[]) {
		{
			SUBPLATFORM(meteorlake, u),
			.pciidlist = mtl_u_ids,
		},
		{},
	}
};

static const struct platform_desc lnl_desc = {
	PLATFORM(lunarlake),
};

static const struct platform_desc bmg_desc = {
	PLATFORM(battlemage),
	PLATFORM_GROUP(dgfx),
};

static const struct platform_desc ptl_desc = {
	PLATFORM(pantherlake),
};

__diag_pop();

/*
 * Separate detection for no display cases to keep the display id array simple.
 *
 * IVB Q requires subvendor and subdevice matching to differentiate from IVB D
 * GT2 server.
 */
static bool has_no_display(struct pci_dev *pdev)
{
	static const struct pci_device_id ids[] = {
		INTEL_IVB_Q_IDS(INTEL_VGA_DEVICE, 0),
		{}
	};

	return pci_match_id(ids, pdev);
}

#define INTEL_DISPLAY_DEVICE(_id, _desc) { .devid = (_id), .desc = (_desc) }

static const struct {
	u32 devid;
	const struct platform_desc *desc;
} intel_display_ids[] = {
	INTEL_I830_IDS(INTEL_DISPLAY_DEVICE, &i830_desc),
	INTEL_I845G_IDS(INTEL_DISPLAY_DEVICE, &i845_desc),
	INTEL_I85X_IDS(INTEL_DISPLAY_DEVICE, &i85x_desc),
	INTEL_I865G_IDS(INTEL_DISPLAY_DEVICE, &i865g_desc),
	INTEL_I915G_IDS(INTEL_DISPLAY_DEVICE, &i915g_desc),
	INTEL_I915GM_IDS(INTEL_DISPLAY_DEVICE, &i915gm_desc),
	INTEL_I945G_IDS(INTEL_DISPLAY_DEVICE, &i945g_desc),
	INTEL_I945GM_IDS(INTEL_DISPLAY_DEVICE, &i945gm_desc),
	INTEL_I965G_IDS(INTEL_DISPLAY_DEVICE, &i965g_desc),
	INTEL_G33_IDS(INTEL_DISPLAY_DEVICE, &g33_desc),
	INTEL_I965GM_IDS(INTEL_DISPLAY_DEVICE, &i965gm_desc),
	INTEL_GM45_IDS(INTEL_DISPLAY_DEVICE, &gm45_desc),
	INTEL_G45_IDS(INTEL_DISPLAY_DEVICE, &g45_desc),
	INTEL_PNV_G_IDS(INTEL_DISPLAY_DEVICE, &pnv_g_desc),
	INTEL_PNV_M_IDS(INTEL_DISPLAY_DEVICE, &pnv_m_desc),
	INTEL_ILK_D_IDS(INTEL_DISPLAY_DEVICE, &ilk_d_desc),
	INTEL_ILK_M_IDS(INTEL_DISPLAY_DEVICE, &ilk_m_desc),
	INTEL_SNB_D_IDS(INTEL_DISPLAY_DEVICE, &snb_d_desc),
	INTEL_SNB_M_IDS(INTEL_DISPLAY_DEVICE, &snb_m_desc),
	INTEL_IVB_D_IDS(INTEL_DISPLAY_DEVICE, &ivb_d_desc),
	INTEL_IVB_M_IDS(INTEL_DISPLAY_DEVICE, &ivb_m_desc),
	INTEL_HSW_IDS(INTEL_DISPLAY_DEVICE, &hsw_desc),
	INTEL_VLV_IDS(INTEL_DISPLAY_DEVICE, &vlv_desc),
	INTEL_BDW_IDS(INTEL_DISPLAY_DEVICE, &bdw_desc),
	INTEL_CHV_IDS(INTEL_DISPLAY_DEVICE, &chv_desc),
	INTEL_SKL_IDS(INTEL_DISPLAY_DEVICE, &skl_desc),
	INTEL_BXT_IDS(INTEL_DISPLAY_DEVICE, &bxt_desc),
	INTEL_GLK_IDS(INTEL_DISPLAY_DEVICE, &glk_desc),
	INTEL_KBL_IDS(INTEL_DISPLAY_DEVICE, &kbl_desc),
	INTEL_CFL_IDS(INTEL_DISPLAY_DEVICE, &cfl_desc),
	INTEL_WHL_IDS(INTEL_DISPLAY_DEVICE, &cfl_desc),
	INTEL_CML_IDS(INTEL_DISPLAY_DEVICE, &cml_desc),
	INTEL_ICL_IDS(INTEL_DISPLAY_DEVICE, &icl_desc),
	INTEL_EHL_IDS(INTEL_DISPLAY_DEVICE, &ehl_desc),
	INTEL_JSL_IDS(INTEL_DISPLAY_DEVICE, &jsl_desc),
	INTEL_TGL_IDS(INTEL_DISPLAY_DEVICE, &tgl_desc),
	INTEL_DG1_IDS(INTEL_DISPLAY_DEVICE, &dg1_desc),
	INTEL_RKL_IDS(INTEL_DISPLAY_DEVICE, &rkl_desc),
	INTEL_ADLS_IDS(INTEL_DISPLAY_DEVICE, &adl_s_desc),
	INTEL_RPLS_IDS(INTEL_DISPLAY_DEVICE, &adl_s_desc),
	INTEL_ADLP_IDS(INTEL_DISPLAY_DEVICE, &adl_p_desc),
	INTEL_ADLN_IDS(INTEL_DISPLAY_DEVICE, &adl_p_desc),
	INTEL_RPLU_IDS(INTEL_DISPLAY_DEVICE, &adl_p_desc),
	INTEL_RPLP_IDS(INTEL_DISPLAY_DEVICE, &adl_p_desc),
	INTEL_DG2_IDS(INTEL_DISPLAY_DEVICE, &dg2_desc),
	INTEL_ARL_IDS(INTEL_DISPLAY_DEVICE, &mtl_desc),
	INTEL_MTL_IDS(INTEL_DISPLAY_DEVICE, &mtl_desc),
	INTEL_LNL_IDS(INTEL_DISPLAY_DEVICE, &lnl_desc),
	INTEL_BMG_IDS(INTEL_DISPLAY_DEVICE, &bmg_desc),
	INTEL_PTL_IDS(INTEL_DISPLAY_DEVICE, &ptl_desc),
};

static const struct {
	u16 ver;
	u16 rel;
	const struct intel_display_device_info *display;
} gmdid_display_map[] = {
	{ 14,  0, &xe_lpdp_display },
	{ 14,  1, &xe2_hpd_display },
	{ 20,  0, &xe2_lpd_display },
	{ 30,  0, &xe2_lpd_display },
};

static const struct intel_display_device_info *
probe_gmdid_display(struct intel_display *display, struct intel_display_ip_ver *ip_ver)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);
	struct intel_display_ip_ver gmd_id;
	void __iomem *addr;
	u32 val;
	int i;

	addr = pci_iomap_range(pdev, 0, i915_mmio_reg_offset(GMD_ID_DISPLAY), sizeof(u32));
	if (!addr) {
		drm_err(display->drm,
			"Cannot map MMIO BAR to read display GMD_ID\n");
		return NULL;
	}

	val = ioread32(addr);
	pci_iounmap(pdev, addr);

	if (val == 0) {
		drm_dbg_kms(display->drm, "Device doesn't have display\n");
		return NULL;
	}

	gmd_id.ver = REG_FIELD_GET(GMD_ID_ARCH_MASK, val);
	gmd_id.rel = REG_FIELD_GET(GMD_ID_RELEASE_MASK, val);
	gmd_id.step = REG_FIELD_GET(GMD_ID_STEP, val);

	for (i = 0; i < ARRAY_SIZE(gmdid_display_map); i++) {
		if (gmd_id.ver == gmdid_display_map[i].ver &&
		    gmd_id.rel == gmdid_display_map[i].rel) {
			*ip_ver = gmd_id;
			return gmdid_display_map[i].display;
		}
	}

	drm_err(display->drm,
		"Unrecognized display IP version %d.%02d; disabling display.\n",
		gmd_id.ver, gmd_id.rel);
	return NULL;
}

static const struct platform_desc *find_platform_desc(struct pci_dev *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(intel_display_ids); i++) {
		if (intel_display_ids[i].devid == pdev->device)
			return intel_display_ids[i].desc;
	}

	return NULL;
}

static const struct subplatform_desc *
find_subplatform_desc(struct pci_dev *pdev, const struct platform_desc *desc)
{
	const struct subplatform_desc *sp;
	const u16 *id;

	for (sp = desc->subplatforms; sp && sp->pciidlist; sp++)
		for (id = sp->pciidlist; *id; id++)
			if (*id == pdev->device)
				return sp;

	return NULL;
}

static enum intel_step get_pre_gmdid_step(struct intel_display *display,
					  const struct stepping_desc *main,
					  const struct stepping_desc *sub)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);
	const enum intel_step *map = main->map;
	int size = main->size;
	int revision = pdev->revision;
	enum intel_step step;

	/* subplatform stepping info trumps main platform info */
	if (sub && sub->map && sub->size) {
		map = sub->map;
		size = sub->size;
	}

	/* not all platforms define steppings, and it's fine */
	if (!map || !size)
		return STEP_NONE;

	if (revision < size && map[revision] != STEP_NONE) {
		step = map[revision];
	} else {
		drm_warn(display->drm, "Unknown revision 0x%02x\n", revision);

		/*
		 * If we hit a gap in the revision to step map, use the information
		 * for the next revision.
		 *
		 * This may be wrong in all sorts of ways, especially if the
		 * steppings in the array are not monotonically increasing, but
		 * it's better than defaulting to 0.
		 */
		while (revision < size && map[revision] == STEP_NONE)
			revision++;

		if (revision < size) {
			drm_dbg_kms(display->drm, "Using display stepping for revision 0x%02x\n",
				    revision);
			step = map[revision];
		} else {
			drm_dbg_kms(display->drm, "Using future display stepping\n");
			step = STEP_FUTURE;
		}
	}

	drm_WARN_ON(display->drm, step == STEP_NONE);

	return step;
}

/* Size of the entire bitmap, not the number of platforms */
static unsigned int display_platforms_num_bits(void)
{
	return sizeof(((struct intel_display_platforms *)0)->bitmap) * BITS_PER_BYTE;
}

/* Number of platform bits set */
static unsigned int display_platforms_weight(const struct intel_display_platforms *p)
{
	return bitmap_weight(p->bitmap, display_platforms_num_bits());
}

/* Merge the subplatform information from src to dst */
static void display_platforms_or(struct intel_display_platforms *dst,
				 const struct intel_display_platforms *src)
{
	bitmap_or(dst->bitmap, dst->bitmap, src->bitmap, display_platforms_num_bits());
}

struct intel_display *intel_display_device_probe(struct pci_dev *pdev)
{
	struct intel_display *display = to_intel_display(pdev);
	const struct intel_display_device_info *info;
	struct intel_display_ip_ver ip_ver = {};
	const struct platform_desc *desc;
	const struct subplatform_desc *subdesc;
	enum intel_step step;

	/* Add drm device backpointer as early as possible. */
	display->drm = pci_get_drvdata(pdev);

	intel_display_params_copy(&display->params);

	if (has_no_display(pdev)) {
		drm_dbg_kms(display->drm, "Device doesn't have display\n");
		goto no_display;
	}

	desc = find_platform_desc(pdev);
	if (!desc) {
		drm_dbg_kms(display->drm,
			    "Unknown device ID %04x; disabling display.\n",
			    pdev->device);
		goto no_display;
	}

	info = desc->info;
	if (!info)
		info = probe_gmdid_display(display, &ip_ver);
	if (!info)
		goto no_display;

	DISPLAY_INFO(display) = info;

	memcpy(DISPLAY_RUNTIME_INFO(display),
	       &DISPLAY_INFO(display)->__runtime_defaults,
	       sizeof(*DISPLAY_RUNTIME_INFO(display)));

	drm_WARN_ON(display->drm, !desc->name ||
		    !display_platforms_weight(&desc->platforms));

	display->platform = desc->platforms;

	subdesc = find_subplatform_desc(pdev, desc);
	if (subdesc) {
		drm_WARN_ON(display->drm, !subdesc->name ||
			    !display_platforms_weight(&subdesc->platforms));

		display_platforms_or(&display->platform, &subdesc->platforms);

		/* Ensure platform and subplatform are distinct */
		drm_WARN_ON(display->drm,
			    display_platforms_weight(&display->platform) !=
			    display_platforms_weight(&desc->platforms) +
			    display_platforms_weight(&subdesc->platforms));
	}

	if (ip_ver.ver || ip_ver.rel || ip_ver.step) {
		DISPLAY_RUNTIME_INFO(display)->ip = ip_ver;
		step = STEP_A0 + ip_ver.step;
		if (step > STEP_FUTURE) {
			drm_dbg_kms(display->drm, "Using future display stepping\n");
			step = STEP_FUTURE;
		}
	} else {
		step = get_pre_gmdid_step(display, &desc->step_info,
					  subdesc ? &subdesc->step_info : NULL);
	}

	DISPLAY_RUNTIME_INFO(display)->step = step;

	drm_info(display->drm, "Found %s%s%s (device ID %04x) %s display version %u.%02u stepping %s\n",
		 desc->name, subdesc ? "/" : "", subdesc ? subdesc->name : "",
		 pdev->device, display->platform.dgfx ? "discrete" : "integrated",
		 DISPLAY_RUNTIME_INFO(display)->ip.ver,
		 DISPLAY_RUNTIME_INFO(display)->ip.rel,
		 step != STEP_NONE ? intel_step_name(step) : "N/A");

	return display;

no_display:
	DISPLAY_INFO(display) = &no_display;

	return display;
}

void intel_display_device_remove(struct intel_display *display)
{
	intel_display_params_free(&display->params);
}

static void __intel_display_device_info_runtime_init(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct intel_display_runtime_info *display_runtime = DISPLAY_RUNTIME_INFO(display);
	enum pipe pipe;

	BUILD_BUG_ON(BITS_PER_TYPE(display_runtime->pipe_mask) < I915_MAX_PIPES);
	BUILD_BUG_ON(BITS_PER_TYPE(display_runtime->cpu_transcoder_mask) < I915_MAX_TRANSCODERS);
	BUILD_BUG_ON(BITS_PER_TYPE(display_runtime->port_mask) < I915_MAX_PORTS);

	/* This covers both ULT and ULX */
	if (display->platform.haswell_ult || display->platform.broadwell_ult)
		display_runtime->port_mask &= ~BIT(PORT_D);

	if (display->platform.icelake_port_f)
		display_runtime->port_mask |= BIT(PORT_F);

	/* Wa_14011765242: adl-s A0,A1 */
	if (display->platform.alderlake_s && IS_DISPLAY_STEP(display, STEP_A0, STEP_A2))
		for_each_pipe(display, pipe)
			display_runtime->num_scalers[pipe] = 0;
	else if (DISPLAY_VER(display) >= 11) {
		for_each_pipe(display, pipe)
			display_runtime->num_scalers[pipe] = 2;
	} else if (DISPLAY_VER(display) >= 9) {
		display_runtime->num_scalers[PIPE_A] = 2;
		display_runtime->num_scalers[PIPE_B] = 2;
		display_runtime->num_scalers[PIPE_C] = 1;
	}

	if (DISPLAY_VER(display) >= 13 || HAS_D12_PLANE_MINIMIZATION(display))
		for_each_pipe(display, pipe)
			display_runtime->num_sprites[pipe] = 4;
	else if (DISPLAY_VER(display) >= 11)
		for_each_pipe(display, pipe)
			display_runtime->num_sprites[pipe] = 6;
	else if (DISPLAY_VER(display) == 10)
		for_each_pipe(display, pipe)
			display_runtime->num_sprites[pipe] = 3;
	else if (display->platform.broxton) {
		/*
		 * Skylake and Broxton currently don't expose the topmost plane as its
		 * use is exclusive with the legacy cursor and we only want to expose
		 * one of those, not both. Until we can safely expose the topmost plane
		 * as a DRM_PLANE_TYPE_CURSOR with all the features exposed/supported,
		 * we don't expose the topmost plane at all to prevent ABI breakage
		 * down the line.
		 */

		display_runtime->num_sprites[PIPE_A] = 2;
		display_runtime->num_sprites[PIPE_B] = 2;
		display_runtime->num_sprites[PIPE_C] = 1;
	} else if (display->platform.valleyview || display->platform.cherryview) {
		for_each_pipe(display, pipe)
			display_runtime->num_sprites[pipe] = 2;
	} else if (DISPLAY_VER(display) >= 5 || display->platform.g4x) {
		for_each_pipe(display, pipe)
			display_runtime->num_sprites[pipe] = 1;
	}

	if ((display->platform.dgfx || DISPLAY_VER(display) >= 14) &&
	    !(intel_de_read(display, GU_CNTL_PROTECTED) & DEPRESENT)) {
		drm_info(display->drm, "Display not present, disabling\n");
		goto display_fused_off;
	}

	if (IS_DISPLAY_VER(display, 7, 8) && HAS_PCH_SPLIT(i915)) {
		u32 fuse_strap = intel_de_read(display, FUSE_STRAP);
		u32 sfuse_strap = intel_de_read(display, SFUSE_STRAP);

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
		    (HAS_PCH_CPT(i915) &&
		     !(sfuse_strap & SFUSE_STRAP_FUSE_LOCK))) {
			drm_info(display->drm,
				 "Display fused off, disabling\n");
			goto display_fused_off;
		} else if (fuse_strap & IVB_PIPE_C_DISABLE) {
			drm_info(display->drm, "PipeC fused off\n");
			display_runtime->pipe_mask &= ~BIT(PIPE_C);
			display_runtime->cpu_transcoder_mask &= ~BIT(TRANSCODER_C);
		}
	} else if (DISPLAY_VER(display) >= 9) {
		u32 dfsm = intel_de_read(display, SKL_DFSM);

		if (dfsm & SKL_DFSM_PIPE_A_DISABLE) {
			display_runtime->pipe_mask &= ~BIT(PIPE_A);
			display_runtime->cpu_transcoder_mask &= ~BIT(TRANSCODER_A);
			display_runtime->fbc_mask &= ~BIT(INTEL_FBC_A);
		}
		if (dfsm & SKL_DFSM_PIPE_B_DISABLE) {
			display_runtime->pipe_mask &= ~BIT(PIPE_B);
			display_runtime->cpu_transcoder_mask &= ~BIT(TRANSCODER_B);
			display_runtime->fbc_mask &= ~BIT(INTEL_FBC_B);
		}
		if (dfsm & SKL_DFSM_PIPE_C_DISABLE) {
			display_runtime->pipe_mask &= ~BIT(PIPE_C);
			display_runtime->cpu_transcoder_mask &= ~BIT(TRANSCODER_C);
			display_runtime->fbc_mask &= ~BIT(INTEL_FBC_C);
		}

		if (DISPLAY_VER(display) >= 12 &&
		    (dfsm & TGL_DFSM_PIPE_D_DISABLE)) {
			display_runtime->pipe_mask &= ~BIT(PIPE_D);
			display_runtime->cpu_transcoder_mask &= ~BIT(TRANSCODER_D);
			display_runtime->fbc_mask &= ~BIT(INTEL_FBC_D);
		}

		if (!display_runtime->pipe_mask)
			goto display_fused_off;

		if (dfsm & SKL_DFSM_DISPLAY_HDCP_DISABLE)
			display_runtime->has_hdcp = 0;

		if (display->platform.dg2 || DISPLAY_VER(display) < 13) {
			if (dfsm & SKL_DFSM_DISPLAY_PM_DISABLE)
				display_runtime->fbc_mask = 0;
		}

		if (DISPLAY_VER(display) >= 11 && (dfsm & ICL_DFSM_DMC_DISABLE))
			display_runtime->has_dmc = 0;

		if (IS_DISPLAY_VER(display, 10, 12) &&
		    (dfsm & GLK_DFSM_DISPLAY_DSC_DISABLE))
			display_runtime->has_dsc = 0;

		if (DISPLAY_VER(display) >= 20 &&
		    (dfsm & XE2LPD_DFSM_DBUF_OVERLAP_DISABLE))
			display_runtime->has_dbuf_overlap_detection = false;
	}

	if (DISPLAY_VER(display) >= 20) {
		u32 cap = intel_de_read(display, XE2LPD_DE_CAP);

		if (REG_FIELD_GET(XE2LPD_DE_CAP_DSC_MASK, cap) ==
		    XE2LPD_DE_CAP_DSC_REMOVED)
			display_runtime->has_dsc = 0;

		if (REG_FIELD_GET(XE2LPD_DE_CAP_SCALER_MASK, cap) ==
		    XE2LPD_DE_CAP_SCALER_SINGLE) {
			for_each_pipe(display, pipe)
				if (display_runtime->num_scalers[pipe])
					display_runtime->num_scalers[pipe] = 1;
		}
	}

	if (DISPLAY_VER(display) >= 30)
		display_runtime->edp_typec_support =
			intel_de_read(display, PICA_PHY_CONFIG_CONTROL) & EDP_ON_TYPEC;

	display_runtime->rawclk_freq = intel_read_rawclk(display);
	drm_dbg_kms(display->drm, "rawclk rate: %d kHz\n",
		    display_runtime->rawclk_freq);

	return;

display_fused_off:
	memset(display_runtime, 0, sizeof(*display_runtime));
}

void intel_display_device_info_runtime_init(struct intel_display *display)
{
	if (HAS_DISPLAY(display))
		__intel_display_device_info_runtime_init(display);

	/* Display may have been disabled by runtime init */
	if (!HAS_DISPLAY(display)) {
		display->drm->driver_features &= ~(DRIVER_MODESET | DRIVER_ATOMIC);
		display->info.__device_info = &no_display;
	}

	/* Disable nuclear pageflip by default on pre-g4x */
	if (!display->params.nuclear_pageflip &&
	    DISPLAY_VER(display) < 5 && !display->platform.g4x)
		display->drm->driver_features &= ~DRIVER_ATOMIC;
}

void intel_display_device_info_print(const struct intel_display_device_info *info,
				     const struct intel_display_runtime_info *runtime,
				     struct drm_printer *p)
{
	if (runtime->ip.rel)
		drm_printf(p, "display version: %u.%02u\n",
			   runtime->ip.ver,
			   runtime->ip.rel);
	else
		drm_printf(p, "display version: %u\n",
			   runtime->ip.ver);

	drm_printf(p, "display stepping: %s\n", intel_step_name(runtime->step));

#define PRINT_FLAG(name) drm_printf(p, "%s: %s\n", #name, str_yes_no(info->name))
	DEV_INFO_DISPLAY_FOR_EACH_FLAG(PRINT_FLAG);
#undef PRINT_FLAG

	drm_printf(p, "has_hdcp: %s\n", str_yes_no(runtime->has_hdcp));
	drm_printf(p, "has_dmc: %s\n", str_yes_no(runtime->has_dmc));
	drm_printf(p, "has_dsc: %s\n", str_yes_no(runtime->has_dsc));

	drm_printf(p, "rawclk rate: %u kHz\n", runtime->rawclk_freq);
}

/*
 * Assuming the device has display hardware, should it be enabled?
 *
 * It's an error to call this function if the device does not have display
 * hardware.
 *
 * Disabling display means taking over the display hardware, putting it to
 * sleep, and preventing connectors from being connected via any means.
 */
bool intel_display_device_enabled(struct intel_display *display)
{
	/* Only valid when HAS_DISPLAY() is true */
	drm_WARN_ON(display->drm, !HAS_DISPLAY(display));

	return !display->params.disable_display &&
		!intel_opregion_headless_sku(display);
}

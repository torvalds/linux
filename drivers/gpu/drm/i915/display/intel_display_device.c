// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/i915_pciids.h>
#include <drm/drm_color_mgmt.h>
#include <linux/pci.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_device.h"
#include "intel_display_params.h"
#include "intel_display_power.h"
#include "intel_display_reg_defs.h"
#include "intel_fbc.h"

__diag_push();
__diag_ignore_all("-Woverride-init", "Allow field initialization overrides for display info");

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

static const struct intel_display_device_info i830_display = {
	I830_DISPLAY,

	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C), /* DVO A/B/C */
};

static const struct intel_display_device_info i845_display = {
	I845_DISPLAY,

	.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* DVO B/C */
};

static const struct intel_display_device_info i85x_display = {
	I830_DISPLAY,

	.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* DVO B/C */
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
};

static const struct intel_display_device_info i865g_display = {
	I845_DISPLAY,

	.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* DVO B/C */
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
};

#define GEN3_DISPLAY \
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

static const struct intel_display_device_info i915g_display = {
	GEN3_DISPLAY,
	I845_COLORS,
	.cursor_needs_physical = 1,
	.overlay_needs_physical = 1,
};

static const struct intel_display_device_info i915gm_display = {
	GEN3_DISPLAY,
	I9XX_COLORS,
	.cursor_needs_physical = 1,
	.overlay_needs_physical = 1,
	.supports_tv = 1,

	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
};

static const struct intel_display_device_info i945g_display = {
	GEN3_DISPLAY,
	I845_COLORS,
	.has_hotplug = 1,
	.cursor_needs_physical = 1,
	.overlay_needs_physical = 1,
};

static const struct intel_display_device_info i945gm_display = {
	GEN3_DISPLAY,
	I9XX_COLORS,
	.has_hotplug = 1,
	.cursor_needs_physical = 1,
	.overlay_needs_physical = 1,
	.supports_tv = 1,

	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
};

static const struct intel_display_device_info g33_display = {
	GEN3_DISPLAY,
	I845_COLORS,
	.has_hotplug = 1,
};

static const struct intel_display_device_info pnv_display = {
	GEN3_DISPLAY,
	I9XX_COLORS,
	.has_hotplug = 1,
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

static const struct intel_display_device_info i965g_display = {
	GEN4_DISPLAY,
	.has_overlay = 1,

	.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* SDVO B/C */
};

static const struct intel_display_device_info i965gm_display = {
	GEN4_DISPLAY,
	.has_overlay = 1,
	.supports_tv = 1,

	.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C), /* SDVO B/C */
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
};

static const struct intel_display_device_info g45_display = {
	GEN4_DISPLAY,

	.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D), /* SDVO/HDMI/DP B/C, DP D */
};

static const struct intel_display_device_info gm45_display = {
	GEN4_DISPLAY,
	.supports_tv = 1,

	.__runtime_defaults.port_mask = BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D), /* SDVO/HDMI/DP B/C, DP D */
	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
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

static const struct intel_display_device_info ilk_d_display = {
	ILK_DISPLAY,
};

static const struct intel_display_device_info ilk_m_display = {
	ILK_DISPLAY,

	.__runtime_defaults.fbc_mask = BIT(INTEL_FBC_A),
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

static const struct intel_display_device_info vlv_display = {
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
};

static const struct intel_display_device_info hsw_display = {
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
};

static const struct intel_display_device_info bdw_display = {
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
};

static const struct intel_display_device_info chv_display = {
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

#define GEN9_LP_DISPLAY \
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

static const struct intel_display_device_info bxt_display = {
	GEN9_LP_DISPLAY,
	.dbuf.size = 512 - 4, /* 4 blocks for bypass path allocation */

	.__runtime_defaults.ip.ver = 9,
};

static const struct intel_display_device_info glk_display = {
	GEN9_LP_DISPLAY,
	.dbuf.size = 1024 - 4, /* 4 blocks for bypass path allocation */
	GLK_COLORS,

	.__runtime_defaults.ip.ver = 10,
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

static const struct intel_display_device_info icl_display = {
	ICL_DISPLAY,

	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D) | BIT(PORT_E),
};

static const struct intel_display_device_info jsl_ehl_display = {
	ICL_DISPLAY,

	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D),
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

static const struct intel_display_device_info tgl_display = {
	XE_D_DISPLAY,

	/*
	 * FIXME DDI C/combo PHY C missing due to combo PHY
	 * code making a mess on SKUs where the PHY is missing.
	 */
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) |
		BIT(PORT_TC1) | BIT(PORT_TC2) | BIT(PORT_TC3) | BIT(PORT_TC4) | BIT(PORT_TC5) | BIT(PORT_TC6),
};

static const struct intel_display_device_info dg1_display = {
	XE_D_DISPLAY,

	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) |
		BIT(PORT_TC1) | BIT(PORT_TC2),
};

static const struct intel_display_device_info rkl_display = {
	XE_D_DISPLAY,
	.abox_mask = BIT(0),
	.has_hti = 1,
	.has_psr_hw_tracking = 0,

	.__runtime_defaults.pipe_mask = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
	.__runtime_defaults.cpu_transcoder_mask =
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) | BIT(TRANSCODER_C),
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) |
		BIT(PORT_TC1) | BIT(PORT_TC2),
};

static const struct intel_display_device_info adl_s_display = {
	XE_D_DISPLAY,
	.has_hti = 1,
	.has_psr_hw_tracking = 0,

	.__runtime_defaults.port_mask = BIT(PORT_A) |
		BIT(PORT_TC1) | BIT(PORT_TC2) | BIT(PORT_TC3) | BIT(PORT_TC4),
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

static const struct intel_display_device_info xe_hpd_display = {
	XE_LPD_FEATURES,
	.has_cdclk_squash = 1,

	.__runtime_defaults.cpu_transcoder_mask =
		BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D),
	.__runtime_defaults.port_mask = BIT(PORT_A) | BIT(PORT_B) | BIT(PORT_C) | BIT(PORT_D_XELPD) |
		BIT(PORT_TC1),
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
};

static const struct intel_display_device_info xe2_hpd_display = {
	XE_LPDP_FEATURES,
	.__runtime_defaults.port_mask = BIT(PORT_A) |
		BIT(PORT_TC1) | BIT(PORT_TC2) | BIT(PORT_TC3) | BIT(PORT_TC4),
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

#define INTEL_DISPLAY_DEVICE(_id, _info) { .devid = (_id), .info = (_info) }

static const struct {
	u32 devid;
	const struct intel_display_device_info *info;
} intel_display_ids[] = {
	INTEL_I830_IDS(INTEL_DISPLAY_DEVICE, &i830_display),
	INTEL_I845G_IDS(INTEL_DISPLAY_DEVICE, &i845_display),
	INTEL_I85X_IDS(INTEL_DISPLAY_DEVICE, &i85x_display),
	INTEL_I865G_IDS(INTEL_DISPLAY_DEVICE, &i865g_display),
	INTEL_I915G_IDS(INTEL_DISPLAY_DEVICE, &i915g_display),
	INTEL_I915GM_IDS(INTEL_DISPLAY_DEVICE, &i915gm_display),
	INTEL_I945G_IDS(INTEL_DISPLAY_DEVICE, &i945g_display),
	INTEL_I945GM_IDS(INTEL_DISPLAY_DEVICE, &i945gm_display),
	INTEL_I965G_IDS(INTEL_DISPLAY_DEVICE, &i965g_display),
	INTEL_G33_IDS(INTEL_DISPLAY_DEVICE, &g33_display),
	INTEL_I965GM_IDS(INTEL_DISPLAY_DEVICE, &i965gm_display),
	INTEL_GM45_IDS(INTEL_DISPLAY_DEVICE, &gm45_display),
	INTEL_G45_IDS(INTEL_DISPLAY_DEVICE, &g45_display),
	INTEL_PNV_IDS(INTEL_DISPLAY_DEVICE, &pnv_display),
	INTEL_ILK_D_IDS(INTEL_DISPLAY_DEVICE, &ilk_d_display),
	INTEL_ILK_M_IDS(INTEL_DISPLAY_DEVICE, &ilk_m_display),
	INTEL_SNB_IDS(INTEL_DISPLAY_DEVICE, &snb_display),
	INTEL_IVB_IDS(INTEL_DISPLAY_DEVICE, &ivb_display),
	INTEL_HSW_IDS(INTEL_DISPLAY_DEVICE, &hsw_display),
	INTEL_VLV_IDS(INTEL_DISPLAY_DEVICE, &vlv_display),
	INTEL_BDW_IDS(INTEL_DISPLAY_DEVICE, &bdw_display),
	INTEL_CHV_IDS(INTEL_DISPLAY_DEVICE, &chv_display),
	INTEL_SKL_IDS(INTEL_DISPLAY_DEVICE, &skl_display),
	INTEL_BXT_IDS(INTEL_DISPLAY_DEVICE, &bxt_display),
	INTEL_GLK_IDS(INTEL_DISPLAY_DEVICE, &glk_display),
	INTEL_KBL_IDS(INTEL_DISPLAY_DEVICE, &skl_display),
	INTEL_CFL_IDS(INTEL_DISPLAY_DEVICE, &skl_display),
	INTEL_WHL_IDS(INTEL_DISPLAY_DEVICE, &skl_display),
	INTEL_CML_IDS(INTEL_DISPLAY_DEVICE, &skl_display),
	INTEL_ICL_IDS(INTEL_DISPLAY_DEVICE, &icl_display),
	INTEL_EHL_IDS(INTEL_DISPLAY_DEVICE, &jsl_ehl_display),
	INTEL_JSL_IDS(INTEL_DISPLAY_DEVICE, &jsl_ehl_display),
	INTEL_TGL_IDS(INTEL_DISPLAY_DEVICE, &tgl_display),
	INTEL_DG1_IDS(INTEL_DISPLAY_DEVICE, &dg1_display),
	INTEL_RKL_IDS(INTEL_DISPLAY_DEVICE, &rkl_display),
	INTEL_ADLS_IDS(INTEL_DISPLAY_DEVICE, &adl_s_display),
	INTEL_RPLS_IDS(INTEL_DISPLAY_DEVICE, &adl_s_display),
	INTEL_ADLP_IDS(INTEL_DISPLAY_DEVICE, &xe_lpd_display),
	INTEL_ADLN_IDS(INTEL_DISPLAY_DEVICE, &xe_lpd_display),
	INTEL_RPLU_IDS(INTEL_DISPLAY_DEVICE, &xe_lpd_display),
	INTEL_RPLP_IDS(INTEL_DISPLAY_DEVICE, &xe_lpd_display),
	INTEL_DG2_IDS(INTEL_DISPLAY_DEVICE, &xe_hpd_display),

	/*
	 * Do not add any GMD_ID-based platforms to this list.  They will
	 * be probed automatically based on the IP version reported by
	 * the hardware.
	 */
};

static const struct {
	u16 ver;
	u16 rel;
	const struct intel_display_device_info *display;
} gmdid_display_map[] = {
	{ 14,  0, &xe_lpdp_display },
	{ 14,  1, &xe2_hpd_display },
	{ 20,  0, &xe2_lpd_display },
};

static const struct intel_display_device_info *
probe_gmdid_display(struct drm_i915_private *i915, u16 *ver, u16 *rel, u16 *step)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	void __iomem *addr;
	u32 val;
	int i;

	/* The caller expects to ver, rel and step to be initialized
	 * here, and there's no good way to check when there was a
	 * failure and no_display was returned.  So initialize all these
	 * values here zero, to be sure.
	 */
	*ver = 0;
	*rel = 0;
	*step = 0;

	addr = pci_iomap_range(pdev, 0, i915_mmio_reg_offset(GMD_ID_DISPLAY), sizeof(u32));
	if (!addr) {
		drm_err(&i915->drm, "Cannot map MMIO BAR to read display GMD_ID\n");
		return &no_display;
	}

	val = ioread32(addr);
	pci_iounmap(pdev, addr);

	if (val == 0) {
		drm_dbg_kms(&i915->drm, "Device doesn't have display\n");
		return &no_display;
	}

	*ver = REG_FIELD_GET(GMD_ID_ARCH_MASK, val);
	*rel = REG_FIELD_GET(GMD_ID_RELEASE_MASK, val);
	*step = REG_FIELD_GET(GMD_ID_STEP, val);

	for (i = 0; i < ARRAY_SIZE(gmdid_display_map); i++)
		if (*ver == gmdid_display_map[i].ver &&
		    *rel == gmdid_display_map[i].rel)
			return gmdid_display_map[i].display;

	drm_err(&i915->drm, "Unrecognized display IP version %d.%02d; disabling display.\n",
		*ver, *rel);
	return &no_display;
}

static const struct intel_display_device_info *
probe_display(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	int i;

	if (has_no_display(pdev)) {
		drm_dbg_kms(&i915->drm, "Device doesn't have display\n");
		return &no_display;
	}

	for (i = 0; i < ARRAY_SIZE(intel_display_ids); i++) {
		if (intel_display_ids[i].devid == pdev->device)
			return intel_display_ids[i].info;
	}

	drm_dbg(&i915->drm, "No display ID found for device ID %04x; disabling display.\n",
		pdev->device);

	return &no_display;
}

void intel_display_device_probe(struct drm_i915_private *i915)
{
	const struct intel_display_device_info *info;
	u16 ver, rel, step;

	/* Add drm device backpointer as early as possible. */
	i915->display.drm = &i915->drm;

	if (HAS_GMD_ID(i915))
		info = probe_gmdid_display(i915, &ver, &rel, &step);
	else
		info = probe_display(i915);

	DISPLAY_INFO(i915) = info;

	memcpy(DISPLAY_RUNTIME_INFO(i915),
	       &DISPLAY_INFO(i915)->__runtime_defaults,
	       sizeof(*DISPLAY_RUNTIME_INFO(i915)));

	if (HAS_GMD_ID(i915)) {
		DISPLAY_RUNTIME_INFO(i915)->ip.ver = ver;
		DISPLAY_RUNTIME_INFO(i915)->ip.rel = rel;
		DISPLAY_RUNTIME_INFO(i915)->ip.step = step;
	}

	intel_display_params_copy(&i915->display.params);
}

void intel_display_device_remove(struct drm_i915_private *i915)
{
	intel_display_params_free(&i915->display.params);
}

static void __intel_display_device_info_runtime_init(struct drm_i915_private *i915)
{
	struct intel_display_runtime_info *display_runtime = DISPLAY_RUNTIME_INFO(i915);
	enum pipe pipe;

	BUILD_BUG_ON(BITS_PER_TYPE(display_runtime->pipe_mask) < I915_MAX_PIPES);
	BUILD_BUG_ON(BITS_PER_TYPE(display_runtime->cpu_transcoder_mask) < I915_MAX_TRANSCODERS);
	BUILD_BUG_ON(BITS_PER_TYPE(display_runtime->port_mask) < I915_MAX_PORTS);

	/* This covers both ULT and ULX */
	if (IS_HASWELL_ULT(i915) || IS_BROADWELL_ULT(i915))
		display_runtime->port_mask &= ~BIT(PORT_D);

	if (IS_ICL_WITH_PORT_F(i915))
		display_runtime->port_mask |= BIT(PORT_F);

	/* Wa_14011765242: adl-s A0,A1 */
	if (IS_ALDERLAKE_S(i915) && IS_DISPLAY_STEP(i915, STEP_A0, STEP_A2))
		for_each_pipe(i915, pipe)
			display_runtime->num_scalers[pipe] = 0;
	else if (DISPLAY_VER(i915) >= 11) {
		for_each_pipe(i915, pipe)
			display_runtime->num_scalers[pipe] = 2;
	} else if (DISPLAY_VER(i915) >= 9) {
		display_runtime->num_scalers[PIPE_A] = 2;
		display_runtime->num_scalers[PIPE_B] = 2;
		display_runtime->num_scalers[PIPE_C] = 1;
	}

	if (DISPLAY_VER(i915) >= 13 || HAS_D12_PLANE_MINIMIZATION(i915))
		for_each_pipe(i915, pipe)
			display_runtime->num_sprites[pipe] = 4;
	else if (DISPLAY_VER(i915) >= 11)
		for_each_pipe(i915, pipe)
			display_runtime->num_sprites[pipe] = 6;
	else if (DISPLAY_VER(i915) == 10)
		for_each_pipe(i915, pipe)
			display_runtime->num_sprites[pipe] = 3;
	else if (IS_BROXTON(i915)) {
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
	} else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915)) {
		for_each_pipe(i915, pipe)
			display_runtime->num_sprites[pipe] = 2;
	} else if (DISPLAY_VER(i915) >= 5 || IS_G4X(i915)) {
		for_each_pipe(i915, pipe)
			display_runtime->num_sprites[pipe] = 1;
	}

	if ((IS_DGFX(i915) || DISPLAY_VER(i915) >= 14) &&
	    !(intel_de_read(i915, GU_CNTL_PROTECTED) & DEPRESENT)) {
		drm_info(&i915->drm, "Display not present, disabling\n");
		goto display_fused_off;
	}

	if (IS_DISPLAY_VER(i915, 7, 8) && HAS_PCH_SPLIT(i915)) {
		u32 fuse_strap = intel_de_read(i915, FUSE_STRAP);
		u32 sfuse_strap = intel_de_read(i915, SFUSE_STRAP);

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
			drm_info(&i915->drm,
				 "Display fused off, disabling\n");
			goto display_fused_off;
		} else if (fuse_strap & IVB_PIPE_C_DISABLE) {
			drm_info(&i915->drm, "PipeC fused off\n");
			display_runtime->pipe_mask &= ~BIT(PIPE_C);
			display_runtime->cpu_transcoder_mask &= ~BIT(TRANSCODER_C);
		}
	} else if (DISPLAY_VER(i915) >= 9) {
		u32 dfsm = intel_de_read(i915, SKL_DFSM);

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

		if (DISPLAY_VER(i915) >= 12 &&
		    (dfsm & TGL_DFSM_PIPE_D_DISABLE)) {
			display_runtime->pipe_mask &= ~BIT(PIPE_D);
			display_runtime->cpu_transcoder_mask &= ~BIT(TRANSCODER_D);
			display_runtime->fbc_mask &= ~BIT(INTEL_FBC_D);
		}

		if (!display_runtime->pipe_mask)
			goto display_fused_off;

		if (dfsm & SKL_DFSM_DISPLAY_HDCP_DISABLE)
			display_runtime->has_hdcp = 0;

		if (dfsm & SKL_DFSM_DISPLAY_PM_DISABLE)
			display_runtime->fbc_mask = 0;

		if (DISPLAY_VER(i915) >= 11 && (dfsm & ICL_DFSM_DMC_DISABLE))
			display_runtime->has_dmc = 0;

		if (IS_DISPLAY_VER(i915, 10, 12) &&
		    (dfsm & GLK_DFSM_DISPLAY_DSC_DISABLE))
			display_runtime->has_dsc = 0;
	}

	if (DISPLAY_VER(i915) >= 20) {
		u32 cap = intel_de_read(i915, XE2LPD_DE_CAP);

		if (REG_FIELD_GET(XE2LPD_DE_CAP_DSC_MASK, cap) ==
		    XE2LPD_DE_CAP_DSC_REMOVED)
			display_runtime->has_dsc = 0;

		if (REG_FIELD_GET(XE2LPD_DE_CAP_SCALER_MASK, cap) ==
		    XE2LPD_DE_CAP_SCALER_SINGLE) {
			for_each_pipe(i915, pipe)
				if (display_runtime->num_scalers[pipe])
					display_runtime->num_scalers[pipe] = 1;
		}
	}

	return;

display_fused_off:
	memset(display_runtime, 0, sizeof(*display_runtime));
}

void intel_display_device_info_runtime_init(struct drm_i915_private *i915)
{
	if (HAS_DISPLAY(i915))
		__intel_display_device_info_runtime_init(i915);

	/* Display may have been disabled by runtime init */
	if (!HAS_DISPLAY(i915)) {
		i915->drm.driver_features &= ~(DRIVER_MODESET | DRIVER_ATOMIC);
		i915->display.info.__device_info = &no_display;
	}

	/* Disable nuclear pageflip by default on pre-g4x */
	if (!i915->display.params.nuclear_pageflip &&
	    DISPLAY_VER(i915) < 5 && !IS_G4X(i915))
		i915->drm.driver_features &= ~DRIVER_ATOMIC;
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

#define PRINT_FLAG(name) drm_printf(p, "%s: %s\n", #name, str_yes_no(info->name))
	DEV_INFO_DISPLAY_FOR_EACH_FLAG(PRINT_FLAG);
#undef PRINT_FLAG

	drm_printf(p, "has_hdcp: %s\n", str_yes_no(runtime->has_hdcp));
	drm_printf(p, "has_dmc: %s\n", str_yes_no(runtime->has_dmc));
	drm_printf(p, "has_dsc: %s\n", str_yes_no(runtime->has_dsc));
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
bool intel_display_device_enabled(struct drm_i915_private *i915)
{
	/* Only valid when HAS_DISPLAY() is true */
	drm_WARN_ON(&i915->drm, !HAS_DISPLAY(i915));

	return !i915->display.params.disable_display &&
		!intel_opregion_headless_sku(i915);
}

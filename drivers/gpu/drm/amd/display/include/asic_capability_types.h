/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
#ifndef __DAL_ASIC_CAPABILITY_TYPES_H__
#define __DAL_ASIC_CAPABILITY_TYPES_H__

/*
 * ASIC Capabilities
 */
struct asic_caps {
	bool CONSUMER_SINGLE_SELECTED_TIMING:1;
	bool UNDERSCAN_ADJUST:1;
	bool DELTA_SIGMA_SUPPORT:1;
	bool PANEL_SELF_REFRESH_SUPPORTED:1;
	bool IS_FUSION:1;
	bool DP_MST_SUPPORTED:1;
	bool UNDERSCAN_FOR_HDMI_ONLY:1;
	bool DVI_CLOCK_SHARE_CAPABILITY:1;
	bool SUPPORT_CEA861E_FINAL:1;
	bool MIRABILIS_SUPPORTED:1;
	bool MIRABILIS_ENABLED_BY_DEFAULT:1;
	bool DEVICE_TAG_REMAP_SUPPORTED:1;
	bool HEADLESS_NO_OPM_SUPPORTED:1;
	bool WIRELESS_LIMIT_TO_720P:1;
	bool WIRELESS_FULL_TIMING_ADJUSTMENT:1;
	bool WIRELESS_TIMING_ADJUSTMENT:1;
	bool WIRELESS_COMPRESSED_AUDIO:1;
	bool VCE_SUPPORTED:1;
	bool HPD_CHECK_FOR_EDID:1;
	bool NEED_MC_TUNING:1;
	bool SKIP_PSR_WAIT_FOR_PLL_LOCK_BIT:1;
	bool DFSBYPASS_DYNAMIC_SUPPORT:1;
	bool SUPPORT_8BPP:1;
};

/*
 * ASIC Stereo 3D Caps
 */
struct asic_stereo_3d_caps {
	bool SUPPORTED:1;
	bool DISPLAY_BASED_ON_WS:1;
	bool HDMI_FRAME_PACK:1;
	bool INTERLACE_FRAME_PACK:1;
	bool DISPLAYPORT_FRAME_PACK:1;
	bool DISPLAYPORT_FRAME_ALT:1;
	bool INTERLEAVE:1;
};

/*
 * ASIC Bugs
 */
struct asic_bugs {
	bool MST_SYMBOL_MISALIGNMENT:1;
	bool PSR_2X_LANE_GANGING:1;
	bool LB_WA_IS_SUPPORTED:1;
	bool ROM_REGISTER_ACCESS:1;
	bool PSR_WA_OVERSCAN_CRC_ERROR:1;
};

/*
 * ASIC Data
 */
enum asic_data {
	ASIC_DATA_FIRST = 0,
	ASIC_DATA_DCE_VERSION = ASIC_DATA_FIRST,
	ASIC_DATA_DCE_VERSION_MINOR,
	ASIC_DATA_LINEBUFFER_SIZE,
	ASIC_DATA_DRAM_BANDWIDTH_EFFICIENCY,
	ASIC_DATA_MC_LATENCY,
	ASIC_DATA_MC_LATENCY_SLOW,
	ASIC_DATA_MEMORYTYPE_MULTIPLIER,
	ASIC_DATA_PATH_NUM_PER_DPMST_CONNECTOR,
	ASIC_DATA_MAX_UNDERSCAN_PERCENTAGE,
	ASIC_DATA_VIEWPORT_PIXEL_GRANULARITY,
	ASIC_DATA_MIN_DISPCLK_FOR_UNDERSCAN,
	ASIC_DATA_DOWNSCALE_LIMIT,
	ASIC_DATA_MAX_NUMBER /* end of enum */
};

/*
 * ASIC Feature Flags
 */
struct asic_feature_flags {
	union {
		uint32_t raw;
		struct {
			uint32_t LEGACY_CLIENT:1;
			uint32_t PACKED_PIXEL_FORMAT:1;
			uint32_t WORKSTATION_STEREO:1;
			uint32_t WORKSTATION:1;
		} bits;
	};
};

#endif /* __DAL_ASIC_CAPABILITY_TYPES_H__ */

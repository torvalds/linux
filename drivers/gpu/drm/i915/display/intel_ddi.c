/*
 * Copyright © 2012 Intel Corporation
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
 * Authors:
 *    Eugeni Dodonov <eugeni.dodonov@intel.com>
 *
 */

#include <drm/drm_scdc_helper.h>

#include "i915_drv.h"
#include "intel_audio.h"
#include "intel_combo_phy.h"
#include "intel_connector.h"
#include "intel_ddi.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_mst.h"
#include "intel_dp_link_training.h"
#include "intel_dpio_phy.h"
#include "intel_dsi.h"
#include "intel_fifo_underrun.h"
#include "intel_gmbus.h"
#include "intel_hdcp.h"
#include "intel_hdmi.h"
#include "intel_hotplug.h"
#include "intel_lspcon.h"
#include "intel_panel.h"
#include "intel_psr.h"
#include "intel_sprite.h"
#include "intel_tc.h"
#include "intel_vdsc.h"

struct ddi_buf_trans {
	u32 trans1;	/* balance leg enable, de-emph level */
	u32 trans2;	/* vref sel, vswing */
	u8 i_boost;	/* SKL: I_boost; valid: 0x0, 0x1, 0x3, 0x7 */
};

static const u8 index_to_dp_signal_levels[] = {
	[0] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[1] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[2] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_2,
	[3] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_3,
	[4] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[5] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[6] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_2,
	[7] = DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[8] = DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[9] = DP_TRAIN_VOLTAGE_SWING_LEVEL_3 | DP_TRAIN_PRE_EMPH_LEVEL_0,
};

/* HDMI/DVI modes ignore everything but the last 2 items. So we share
 * them for both DP and FDI transports, allowing those ports to
 * automatically adapt to HDMI connections as well
 */
static const struct ddi_buf_trans hsw_ddi_translations_dp[] = {
	{ 0x00FFFFFF, 0x0006000E, 0x0 },
	{ 0x00D75FFF, 0x0005000A, 0x0 },
	{ 0x00C30FFF, 0x00040006, 0x0 },
	{ 0x80AAAFFF, 0x000B0000, 0x0 },
	{ 0x00FFFFFF, 0x0005000A, 0x0 },
	{ 0x00D75FFF, 0x000C0004, 0x0 },
	{ 0x80C30FFF, 0x000B0000, 0x0 },
	{ 0x00FFFFFF, 0x00040006, 0x0 },
	{ 0x80D75FFF, 0x000B0000, 0x0 },
};

static const struct ddi_buf_trans hsw_ddi_translations_fdi[] = {
	{ 0x00FFFFFF, 0x0007000E, 0x0 },
	{ 0x00D75FFF, 0x000F000A, 0x0 },
	{ 0x00C30FFF, 0x00060006, 0x0 },
	{ 0x00AAAFFF, 0x001E0000, 0x0 },
	{ 0x00FFFFFF, 0x000F000A, 0x0 },
	{ 0x00D75FFF, 0x00160004, 0x0 },
	{ 0x00C30FFF, 0x001E0000, 0x0 },
	{ 0x00FFFFFF, 0x00060006, 0x0 },
	{ 0x00D75FFF, 0x001E0000, 0x0 },
};

static const struct ddi_buf_trans hsw_ddi_translations_hdmi[] = {
					/* Idx	NT mV d	T mV d	db	*/
	{ 0x00FFFFFF, 0x0006000E, 0x0 },/* 0:	400	400	0	*/
	{ 0x00E79FFF, 0x000E000C, 0x0 },/* 1:	400	500	2	*/
	{ 0x00D75FFF, 0x0005000A, 0x0 },/* 2:	400	600	3.5	*/
	{ 0x00FFFFFF, 0x0005000A, 0x0 },/* 3:	600	600	0	*/
	{ 0x00E79FFF, 0x001D0007, 0x0 },/* 4:	600	750	2	*/
	{ 0x00D75FFF, 0x000C0004, 0x0 },/* 5:	600	900	3.5	*/
	{ 0x00FFFFFF, 0x00040006, 0x0 },/* 6:	800	800	0	*/
	{ 0x80E79FFF, 0x00030002, 0x0 },/* 7:	800	1000	2	*/
	{ 0x00FFFFFF, 0x00140005, 0x0 },/* 8:	850	850	0	*/
	{ 0x00FFFFFF, 0x000C0004, 0x0 },/* 9:	900	900	0	*/
	{ 0x00FFFFFF, 0x001C0003, 0x0 },/* 10:	950	950	0	*/
	{ 0x80FFFFFF, 0x00030002, 0x0 },/* 11:	1000	1000	0	*/
};

static const struct ddi_buf_trans bdw_ddi_translations_edp[] = {
	{ 0x00FFFFFF, 0x00000012, 0x0 },
	{ 0x00EBAFFF, 0x00020011, 0x0 },
	{ 0x00C71FFF, 0x0006000F, 0x0 },
	{ 0x00AAAFFF, 0x000E000A, 0x0 },
	{ 0x00FFFFFF, 0x00020011, 0x0 },
	{ 0x00DB6FFF, 0x0005000F, 0x0 },
	{ 0x00BEEFFF, 0x000A000C, 0x0 },
	{ 0x00FFFFFF, 0x0005000F, 0x0 },
	{ 0x00DB6FFF, 0x000A000C, 0x0 },
};

static const struct ddi_buf_trans bdw_ddi_translations_dp[] = {
	{ 0x00FFFFFF, 0x0007000E, 0x0 },
	{ 0x00D75FFF, 0x000E000A, 0x0 },
	{ 0x00BEFFFF, 0x00140006, 0x0 },
	{ 0x80B2CFFF, 0x001B0002, 0x0 },
	{ 0x00FFFFFF, 0x000E000A, 0x0 },
	{ 0x00DB6FFF, 0x00160005, 0x0 },
	{ 0x80C71FFF, 0x001A0002, 0x0 },
	{ 0x00F7DFFF, 0x00180004, 0x0 },
	{ 0x80D75FFF, 0x001B0002, 0x0 },
};

static const struct ddi_buf_trans bdw_ddi_translations_fdi[] = {
	{ 0x00FFFFFF, 0x0001000E, 0x0 },
	{ 0x00D75FFF, 0x0004000A, 0x0 },
	{ 0x00C30FFF, 0x00070006, 0x0 },
	{ 0x00AAAFFF, 0x000C0000, 0x0 },
	{ 0x00FFFFFF, 0x0004000A, 0x0 },
	{ 0x00D75FFF, 0x00090004, 0x0 },
	{ 0x00C30FFF, 0x000C0000, 0x0 },
	{ 0x00FFFFFF, 0x00070006, 0x0 },
	{ 0x00D75FFF, 0x000C0000, 0x0 },
};

static const struct ddi_buf_trans bdw_ddi_translations_hdmi[] = {
					/* Idx	NT mV d	T mV df	db	*/
	{ 0x00FFFFFF, 0x0007000E, 0x0 },/* 0:	400	400	0	*/
	{ 0x00D75FFF, 0x000E000A, 0x0 },/* 1:	400	600	3.5	*/
	{ 0x00BEFFFF, 0x00140006, 0x0 },/* 2:	400	800	6	*/
	{ 0x00FFFFFF, 0x0009000D, 0x0 },/* 3:	450	450	0	*/
	{ 0x00FFFFFF, 0x000E000A, 0x0 },/* 4:	600	600	0	*/
	{ 0x00D7FFFF, 0x00140006, 0x0 },/* 5:	600	800	2.5	*/
	{ 0x80CB2FFF, 0x001B0002, 0x0 },/* 6:	600	1000	4.5	*/
	{ 0x00FFFFFF, 0x00140006, 0x0 },/* 7:	800	800	0	*/
	{ 0x80E79FFF, 0x001B0002, 0x0 },/* 8:	800	1000	2	*/
	{ 0x80FFFFFF, 0x001B0002, 0x0 },/* 9:	1000	1000	0	*/
};

/* Skylake H and S */
static const struct ddi_buf_trans skl_ddi_translations_dp[] = {
	{ 0x00002016, 0x000000A0, 0x0 },
	{ 0x00005012, 0x0000009B, 0x0 },
	{ 0x00007011, 0x00000088, 0x0 },
	{ 0x80009010, 0x000000C0, 0x1 },
	{ 0x00002016, 0x0000009B, 0x0 },
	{ 0x00005012, 0x00000088, 0x0 },
	{ 0x80007011, 0x000000C0, 0x1 },
	{ 0x00002016, 0x000000DF, 0x0 },
	{ 0x80005012, 0x000000C0, 0x1 },
};

/* Skylake U */
static const struct ddi_buf_trans skl_u_ddi_translations_dp[] = {
	{ 0x0000201B, 0x000000A2, 0x0 },
	{ 0x00005012, 0x00000088, 0x0 },
	{ 0x80007011, 0x000000CD, 0x1 },
	{ 0x80009010, 0x000000C0, 0x1 },
	{ 0x0000201B, 0x0000009D, 0x0 },
	{ 0x80005012, 0x000000C0, 0x1 },
	{ 0x80007011, 0x000000C0, 0x1 },
	{ 0x00002016, 0x00000088, 0x0 },
	{ 0x80005012, 0x000000C0, 0x1 },
};

/* Skylake Y */
static const struct ddi_buf_trans skl_y_ddi_translations_dp[] = {
	{ 0x00000018, 0x000000A2, 0x0 },
	{ 0x00005012, 0x00000088, 0x0 },
	{ 0x80007011, 0x000000CD, 0x3 },
	{ 0x80009010, 0x000000C0, 0x3 },
	{ 0x00000018, 0x0000009D, 0x0 },
	{ 0x80005012, 0x000000C0, 0x3 },
	{ 0x80007011, 0x000000C0, 0x3 },
	{ 0x00000018, 0x00000088, 0x0 },
	{ 0x80005012, 0x000000C0, 0x3 },
};

/* Kabylake H and S */
static const struct ddi_buf_trans kbl_ddi_translations_dp[] = {
	{ 0x00002016, 0x000000A0, 0x0 },
	{ 0x00005012, 0x0000009B, 0x0 },
	{ 0x00007011, 0x00000088, 0x0 },
	{ 0x80009010, 0x000000C0, 0x1 },
	{ 0x00002016, 0x0000009B, 0x0 },
	{ 0x00005012, 0x00000088, 0x0 },
	{ 0x80007011, 0x000000C0, 0x1 },
	{ 0x00002016, 0x00000097, 0x0 },
	{ 0x80005012, 0x000000C0, 0x1 },
};

/* Kabylake U */
static const struct ddi_buf_trans kbl_u_ddi_translations_dp[] = {
	{ 0x0000201B, 0x000000A1, 0x0 },
	{ 0x00005012, 0x00000088, 0x0 },
	{ 0x80007011, 0x000000CD, 0x3 },
	{ 0x80009010, 0x000000C0, 0x3 },
	{ 0x0000201B, 0x0000009D, 0x0 },
	{ 0x80005012, 0x000000C0, 0x3 },
	{ 0x80007011, 0x000000C0, 0x3 },
	{ 0x00002016, 0x0000004F, 0x0 },
	{ 0x80005012, 0x000000C0, 0x3 },
};

/* Kabylake Y */
static const struct ddi_buf_trans kbl_y_ddi_translations_dp[] = {
	{ 0x00001017, 0x000000A1, 0x0 },
	{ 0x00005012, 0x00000088, 0x0 },
	{ 0x80007011, 0x000000CD, 0x3 },
	{ 0x8000800F, 0x000000C0, 0x3 },
	{ 0x00001017, 0x0000009D, 0x0 },
	{ 0x80005012, 0x000000C0, 0x3 },
	{ 0x80007011, 0x000000C0, 0x3 },
	{ 0x00001017, 0x0000004C, 0x0 },
	{ 0x80005012, 0x000000C0, 0x3 },
};

/*
 * Skylake/Kabylake H and S
 * eDP 1.4 low vswing translation parameters
 */
static const struct ddi_buf_trans skl_ddi_translations_edp[] = {
	{ 0x00000018, 0x000000A8, 0x0 },
	{ 0x00004013, 0x000000A9, 0x0 },
	{ 0x00007011, 0x000000A2, 0x0 },
	{ 0x00009010, 0x0000009C, 0x0 },
	{ 0x00000018, 0x000000A9, 0x0 },
	{ 0x00006013, 0x000000A2, 0x0 },
	{ 0x00007011, 0x000000A6, 0x0 },
	{ 0x00000018, 0x000000AB, 0x0 },
	{ 0x00007013, 0x0000009F, 0x0 },
	{ 0x00000018, 0x000000DF, 0x0 },
};

/*
 * Skylake/Kabylake U
 * eDP 1.4 low vswing translation parameters
 */
static const struct ddi_buf_trans skl_u_ddi_translations_edp[] = {
	{ 0x00000018, 0x000000A8, 0x0 },
	{ 0x00004013, 0x000000A9, 0x0 },
	{ 0x00007011, 0x000000A2, 0x0 },
	{ 0x00009010, 0x0000009C, 0x0 },
	{ 0x00000018, 0x000000A9, 0x0 },
	{ 0x00006013, 0x000000A2, 0x0 },
	{ 0x00007011, 0x000000A6, 0x0 },
	{ 0x00002016, 0x000000AB, 0x0 },
	{ 0x00005013, 0x0000009F, 0x0 },
	{ 0x00000018, 0x000000DF, 0x0 },
};

/*
 * Skylake/Kabylake Y
 * eDP 1.4 low vswing translation parameters
 */
static const struct ddi_buf_trans skl_y_ddi_translations_edp[] = {
	{ 0x00000018, 0x000000A8, 0x0 },
	{ 0x00004013, 0x000000AB, 0x0 },
	{ 0x00007011, 0x000000A4, 0x0 },
	{ 0x00009010, 0x000000DF, 0x0 },
	{ 0x00000018, 0x000000AA, 0x0 },
	{ 0x00006013, 0x000000A4, 0x0 },
	{ 0x00007011, 0x0000009D, 0x0 },
	{ 0x00000018, 0x000000A0, 0x0 },
	{ 0x00006012, 0x000000DF, 0x0 },
	{ 0x00000018, 0x0000008A, 0x0 },
};

/* Skylake/Kabylake U, H and S */
static const struct ddi_buf_trans skl_ddi_translations_hdmi[] = {
	{ 0x00000018, 0x000000AC, 0x0 },
	{ 0x00005012, 0x0000009D, 0x0 },
	{ 0x00007011, 0x00000088, 0x0 },
	{ 0x00000018, 0x000000A1, 0x0 },
	{ 0x00000018, 0x00000098, 0x0 },
	{ 0x00004013, 0x00000088, 0x0 },
	{ 0x80006012, 0x000000CD, 0x1 },
	{ 0x00000018, 0x000000DF, 0x0 },
	{ 0x80003015, 0x000000CD, 0x1 },	/* Default */
	{ 0x80003015, 0x000000C0, 0x1 },
	{ 0x80000018, 0x000000C0, 0x1 },
};

/* Skylake/Kabylake Y */
static const struct ddi_buf_trans skl_y_ddi_translations_hdmi[] = {
	{ 0x00000018, 0x000000A1, 0x0 },
	{ 0x00005012, 0x000000DF, 0x0 },
	{ 0x80007011, 0x000000CB, 0x3 },
	{ 0x00000018, 0x000000A4, 0x0 },
	{ 0x00000018, 0x0000009D, 0x0 },
	{ 0x00004013, 0x00000080, 0x0 },
	{ 0x80006013, 0x000000C0, 0x3 },
	{ 0x00000018, 0x0000008A, 0x0 },
	{ 0x80003015, 0x000000C0, 0x3 },	/* Default */
	{ 0x80003015, 0x000000C0, 0x3 },
	{ 0x80000018, 0x000000C0, 0x3 },
};

struct bxt_ddi_buf_trans {
	u8 margin;	/* swing value */
	u8 scale;	/* scale value */
	u8 enable;	/* scale enable */
	u8 deemphasis;
};

static const struct bxt_ddi_buf_trans bxt_ddi_translations_dp[] = {
					/* Idx	NT mV diff	db  */
	{ 52,  0x9A, 0, 128, },	/* 0:	400		0   */
	{ 78,  0x9A, 0, 85,  },	/* 1:	400		3.5 */
	{ 104, 0x9A, 0, 64,  },	/* 2:	400		6   */
	{ 154, 0x9A, 0, 43,  },	/* 3:	400		9.5 */
	{ 77,  0x9A, 0, 128, },	/* 4:	600		0   */
	{ 116, 0x9A, 0, 85,  },	/* 5:	600		3.5 */
	{ 154, 0x9A, 0, 64,  },	/* 6:	600		6   */
	{ 102, 0x9A, 0, 128, },	/* 7:	800		0   */
	{ 154, 0x9A, 0, 85,  },	/* 8:	800		3.5 */
	{ 154, 0x9A, 1, 128, },	/* 9:	1200		0   */
};

static const struct bxt_ddi_buf_trans bxt_ddi_translations_edp[] = {
					/* Idx	NT mV diff	db  */
	{ 26, 0, 0, 128, },	/* 0:	200		0   */
	{ 38, 0, 0, 112, },	/* 1:	200		1.5 */
	{ 48, 0, 0, 96,  },	/* 2:	200		4   */
	{ 54, 0, 0, 69,  },	/* 3:	200		6   */
	{ 32, 0, 0, 128, },	/* 4:	250		0   */
	{ 48, 0, 0, 104, },	/* 5:	250		1.5 */
	{ 54, 0, 0, 85,  },	/* 6:	250		4   */
	{ 43, 0, 0, 128, },	/* 7:	300		0   */
	{ 54, 0, 0, 101, },	/* 8:	300		1.5 */
	{ 48, 0, 0, 128, },	/* 9:	300		0   */
};

/* BSpec has 2 recommended values - entries 0 and 8.
 * Using the entry with higher vswing.
 */
static const struct bxt_ddi_buf_trans bxt_ddi_translations_hdmi[] = {
					/* Idx	NT mV diff	db  */
	{ 52,  0x9A, 0, 128, },	/* 0:	400		0   */
	{ 52,  0x9A, 0, 85,  },	/* 1:	400		3.5 */
	{ 52,  0x9A, 0, 64,  },	/* 2:	400		6   */
	{ 42,  0x9A, 0, 43,  },	/* 3:	400		9.5 */
	{ 77,  0x9A, 0, 128, },	/* 4:	600		0   */
	{ 77,  0x9A, 0, 85,  },	/* 5:	600		3.5 */
	{ 77,  0x9A, 0, 64,  },	/* 6:	600		6   */
	{ 102, 0x9A, 0, 128, },	/* 7:	800		0   */
	{ 102, 0x9A, 0, 85,  },	/* 8:	800		3.5 */
	{ 154, 0x9A, 1, 128, },	/* 9:	1200		0   */
};

struct cnl_ddi_buf_trans {
	u8 dw2_swing_sel;
	u8 dw7_n_scalar;
	u8 dw4_cursor_coeff;
	u8 dw4_post_cursor_2;
	u8 dw4_post_cursor_1;
};

/* Voltage Swing Programming for VccIO 0.85V for DP */
static const struct cnl_ddi_buf_trans cnl_ddi_translations_dp_0_85V[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x5D, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x6A, 0x38, 0x00, 0x07 },	/* 350   500      3.1   */
	{ 0xB, 0x7A, 0x32, 0x00, 0x0D },	/* 350   700      6.0   */
	{ 0x6, 0x7C, 0x2D, 0x00, 0x12 },	/* 350   900      8.2   */
	{ 0xA, 0x69, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xB, 0x7A, 0x36, 0x00, 0x09 },	/* 500   700      2.9   */
	{ 0x6, 0x7C, 0x30, 0x00, 0x0F },	/* 500   900      5.1   */
	{ 0xB, 0x7D, 0x3C, 0x00, 0x03 },	/* 650   725      0.9   */
	{ 0x6, 0x7C, 0x34, 0x00, 0x0B },	/* 600   900      3.5   */
	{ 0x6, 0x7B, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

/* Voltage Swing Programming for VccIO 0.85V for HDMI */
static const struct cnl_ddi_buf_trans cnl_ddi_translations_hdmi_0_85V[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x60, 0x3F, 0x00, 0x00 },	/* 450   450      0.0   */
	{ 0xB, 0x73, 0x36, 0x00, 0x09 },	/* 450   650      3.2   */
	{ 0x6, 0x7F, 0x31, 0x00, 0x0E },	/* 450   850      5.5   */
	{ 0xB, 0x73, 0x3F, 0x00, 0x00 },	/* 650   650      0.0   */
	{ 0x6, 0x7F, 0x37, 0x00, 0x08 },	/* 650   850      2.3   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 850   850      0.0   */
	{ 0x6, 0x7F, 0x35, 0x00, 0x0A },	/* 600   850      3.0   */
};

/* Voltage Swing Programming for VccIO 0.85V for eDP */
static const struct cnl_ddi_buf_trans cnl_ddi_translations_edp_0_85V[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x66, 0x3A, 0x00, 0x05 },	/* 384   500      2.3   */
	{ 0x0, 0x7F, 0x38, 0x00, 0x07 },	/* 153   200      2.3   */
	{ 0x8, 0x7F, 0x38, 0x00, 0x07 },	/* 192   250      2.3   */
	{ 0x1, 0x7F, 0x38, 0x00, 0x07 },	/* 230   300      2.3   */
	{ 0x9, 0x7F, 0x38, 0x00, 0x07 },	/* 269   350      2.3   */
	{ 0xA, 0x66, 0x3C, 0x00, 0x03 },	/* 446   500      1.0   */
	{ 0xB, 0x70, 0x3C, 0x00, 0x03 },	/* 460   600      2.3   */
	{ 0xC, 0x75, 0x3C, 0x00, 0x03 },	/* 537   700      2.3   */
	{ 0x2, 0x7F, 0x3F, 0x00, 0x00 },	/* 400   400      0.0   */
};

/* Voltage Swing Programming for VccIO 0.95V for DP */
static const struct cnl_ddi_buf_trans cnl_ddi_translations_dp_0_95V[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x5D, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x6A, 0x38, 0x00, 0x07 },	/* 350   500      3.1   */
	{ 0xB, 0x7A, 0x32, 0x00, 0x0D },	/* 350   700      6.0   */
	{ 0x6, 0x7C, 0x2D, 0x00, 0x12 },	/* 350   900      8.2   */
	{ 0xA, 0x69, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xB, 0x7A, 0x36, 0x00, 0x09 },	/* 500   700      2.9   */
	{ 0x6, 0x7C, 0x30, 0x00, 0x0F },	/* 500   900      5.1   */
	{ 0xB, 0x7D, 0x3C, 0x00, 0x03 },	/* 650   725      0.9   */
	{ 0x6, 0x7C, 0x34, 0x00, 0x0B },	/* 600   900      3.5   */
	{ 0x6, 0x7B, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

/* Voltage Swing Programming for VccIO 0.95V for HDMI */
static const struct cnl_ddi_buf_trans cnl_ddi_translations_hdmi_0_95V[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x5C, 0x3F, 0x00, 0x00 },	/* 400   400      0.0   */
	{ 0xB, 0x69, 0x37, 0x00, 0x08 },	/* 400   600      3.5   */
	{ 0x5, 0x76, 0x31, 0x00, 0x0E },	/* 400   800      6.0   */
	{ 0xA, 0x5E, 0x3F, 0x00, 0x00 },	/* 450   450      0.0   */
	{ 0xB, 0x69, 0x3F, 0x00, 0x00 },	/* 600   600      0.0   */
	{ 0xB, 0x79, 0x35, 0x00, 0x0A },	/* 600   850      3.0   */
	{ 0x6, 0x7D, 0x32, 0x00, 0x0D },	/* 600   1000     4.4   */
	{ 0x5, 0x76, 0x3F, 0x00, 0x00 },	/* 800   800      0.0   */
	{ 0x6, 0x7D, 0x39, 0x00, 0x06 },	/* 800   1000     1.9   */
	{ 0x6, 0x7F, 0x39, 0x00, 0x06 },	/* 850   1050     1.8   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 1050  1050     0.0   */
};

/* Voltage Swing Programming for VccIO 0.95V for eDP */
static const struct cnl_ddi_buf_trans cnl_ddi_translations_edp_0_95V[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x61, 0x3A, 0x00, 0x05 },	/* 384   500      2.3   */
	{ 0x0, 0x7F, 0x38, 0x00, 0x07 },	/* 153   200      2.3   */
	{ 0x8, 0x7F, 0x38, 0x00, 0x07 },	/* 192   250      2.3   */
	{ 0x1, 0x7F, 0x38, 0x00, 0x07 },	/* 230   300      2.3   */
	{ 0x9, 0x7F, 0x38, 0x00, 0x07 },	/* 269   350      2.3   */
	{ 0xA, 0x61, 0x3C, 0x00, 0x03 },	/* 446   500      1.0   */
	{ 0xB, 0x68, 0x39, 0x00, 0x06 },	/* 460   600      2.3   */
	{ 0xC, 0x6E, 0x39, 0x00, 0x06 },	/* 537   700      2.3   */
	{ 0x4, 0x7F, 0x3A, 0x00, 0x05 },	/* 460   600      2.3   */
	{ 0x2, 0x7F, 0x3F, 0x00, 0x00 },	/* 400   400      0.0   */
};

/* Voltage Swing Programming for VccIO 1.05V for DP */
static const struct cnl_ddi_buf_trans cnl_ddi_translations_dp_1_05V[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x58, 0x3F, 0x00, 0x00 },	/* 400   400      0.0   */
	{ 0xB, 0x64, 0x37, 0x00, 0x08 },	/* 400   600      3.5   */
	{ 0x5, 0x70, 0x31, 0x00, 0x0E },	/* 400   800      6.0   */
	{ 0x6, 0x7F, 0x2C, 0x00, 0x13 },	/* 400   1050     8.4   */
	{ 0xB, 0x64, 0x3F, 0x00, 0x00 },	/* 600   600      0.0   */
	{ 0x5, 0x73, 0x35, 0x00, 0x0A },	/* 600   850      3.0   */
	{ 0x6, 0x7F, 0x30, 0x00, 0x0F },	/* 550   1050     5.6   */
	{ 0x5, 0x76, 0x3E, 0x00, 0x01 },	/* 850   900      0.5   */
	{ 0x6, 0x7F, 0x36, 0x00, 0x09 },	/* 750   1050     2.9   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 1050  1050     0.0   */
};

/* Voltage Swing Programming for VccIO 1.05V for HDMI */
static const struct cnl_ddi_buf_trans cnl_ddi_translations_hdmi_1_05V[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x58, 0x3F, 0x00, 0x00 },	/* 400   400      0.0   */
	{ 0xB, 0x64, 0x37, 0x00, 0x08 },	/* 400   600      3.5   */
	{ 0x5, 0x70, 0x31, 0x00, 0x0E },	/* 400   800      6.0   */
	{ 0xA, 0x5B, 0x3F, 0x00, 0x00 },	/* 450   450      0.0   */
	{ 0xB, 0x64, 0x3F, 0x00, 0x00 },	/* 600   600      0.0   */
	{ 0x5, 0x73, 0x35, 0x00, 0x0A },	/* 600   850      3.0   */
	{ 0x6, 0x7C, 0x32, 0x00, 0x0D },	/* 600   1000     4.4   */
	{ 0x5, 0x70, 0x3F, 0x00, 0x00 },	/* 800   800      0.0   */
	{ 0x6, 0x7C, 0x39, 0x00, 0x06 },	/* 800   1000     1.9   */
	{ 0x6, 0x7F, 0x39, 0x00, 0x06 },	/* 850   1050     1.8   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 1050  1050     0.0   */
};

/* Voltage Swing Programming for VccIO 1.05V for eDP */
static const struct cnl_ddi_buf_trans cnl_ddi_translations_edp_1_05V[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x5E, 0x3A, 0x00, 0x05 },	/* 384   500      2.3   */
	{ 0x0, 0x7F, 0x38, 0x00, 0x07 },	/* 153   200      2.3   */
	{ 0x8, 0x7F, 0x38, 0x00, 0x07 },	/* 192   250      2.3   */
	{ 0x1, 0x7F, 0x38, 0x00, 0x07 },	/* 230   300      2.3   */
	{ 0x9, 0x7F, 0x38, 0x00, 0x07 },	/* 269   350      2.3   */
	{ 0xA, 0x5E, 0x3C, 0x00, 0x03 },	/* 446   500      1.0   */
	{ 0xB, 0x64, 0x39, 0x00, 0x06 },	/* 460   600      2.3   */
	{ 0xE, 0x6A, 0x39, 0x00, 0x06 },	/* 537   700      2.3   */
	{ 0x2, 0x7F, 0x3F, 0x00, 0x00 },	/* 400   400      0.0   */
};

/* icl_combo_phy_ddi_translations */
static const struct cnl_ddi_buf_trans icl_combo_phy_ddi_translations_dp_hbr2[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x35, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x4F, 0x37, 0x00, 0x08 },	/* 350   500      3.1   */
	{ 0xC, 0x71, 0x2F, 0x00, 0x10 },	/* 350   700      6.0   */
	{ 0x6, 0x7F, 0x2B, 0x00, 0x14 },	/* 350   900      8.2   */
	{ 0xA, 0x4C, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x73, 0x34, 0x00, 0x0B },	/* 500   700      2.9   */
	{ 0x6, 0x7F, 0x2F, 0x00, 0x10 },	/* 500   900      5.1   */
	{ 0xC, 0x6C, 0x3C, 0x00, 0x03 },	/* 650   700      0.6   */
	{ 0x6, 0x7F, 0x35, 0x00, 0x0A },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

static const struct cnl_ddi_buf_trans icl_combo_phy_ddi_translations_edp_hbr2[] = {
						/* NT mV Trans mV db    */
	{ 0x0, 0x7F, 0x3F, 0x00, 0x00 },	/* 200   200      0.0   */
	{ 0x8, 0x7F, 0x38, 0x00, 0x07 },	/* 200   250      1.9   */
	{ 0x1, 0x7F, 0x33, 0x00, 0x0C },	/* 200   300      3.5   */
	{ 0x9, 0x7F, 0x31, 0x00, 0x0E },	/* 200   350      4.9   */
	{ 0x8, 0x7F, 0x3F, 0x00, 0x00 },	/* 250   250      0.0   */
	{ 0x1, 0x7F, 0x38, 0x00, 0x07 },	/* 250   300      1.6   */
	{ 0x9, 0x7F, 0x35, 0x00, 0x0A },	/* 250   350      2.9   */
	{ 0x1, 0x7F, 0x3F, 0x00, 0x00 },	/* 300   300      0.0   */
	{ 0x9, 0x7F, 0x38, 0x00, 0x07 },	/* 300   350      1.3   */
	{ 0x9, 0x7F, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
};

static const struct cnl_ddi_buf_trans icl_combo_phy_ddi_translations_edp_hbr3[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x35, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x4F, 0x37, 0x00, 0x08 },	/* 350   500      3.1   */
	{ 0xC, 0x71, 0x2F, 0x00, 0x10 },	/* 350   700      6.0   */
	{ 0x6, 0x7F, 0x2B, 0x00, 0x14 },	/* 350   900      8.2   */
	{ 0xA, 0x4C, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x73, 0x34, 0x00, 0x0B },	/* 500   700      2.9   */
	{ 0x6, 0x7F, 0x2F, 0x00, 0x10 },	/* 500   900      5.1   */
	{ 0xC, 0x6C, 0x3C, 0x00, 0x03 },	/* 650   700      0.6   */
	{ 0x6, 0x7F, 0x35, 0x00, 0x0A },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

static const struct cnl_ddi_buf_trans icl_combo_phy_ddi_translations_hdmi[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x60, 0x3F, 0x00, 0x00 },	/* 450   450      0.0   */
	{ 0xB, 0x73, 0x36, 0x00, 0x09 },	/* 450   650      3.2   */
	{ 0x6, 0x7F, 0x31, 0x00, 0x0E },	/* 450   850      5.5   */
	{ 0xB, 0x73, 0x3F, 0x00, 0x00 },	/* 650   650      0.0   ALS */
	{ 0x6, 0x7F, 0x37, 0x00, 0x08 },	/* 650   850      2.3   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 850   850      0.0   */
	{ 0x6, 0x7F, 0x35, 0x00, 0x0A },	/* 600   850      3.0   */
};

static const struct cnl_ddi_buf_trans ehl_combo_phy_ddi_translations_hbr2_hbr3[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x33, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x47, 0x36, 0x00, 0x09 },	/* 350   500      3.1   */
	{ 0xC, 0x64, 0x30, 0x00, 0x0F },	/* 350   700      6.0   */
	{ 0x6, 0x7F, 0x2C, 0x00, 0x13 },	/* 350   900      8.2   */
	{ 0xA, 0x46, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x64, 0x36, 0x00, 0x09 },	/* 500   700      2.9   */
	{ 0x6, 0x7F, 0x30, 0x00, 0x0F },	/* 500   900      5.1   */
	{ 0xC, 0x61, 0x3F, 0x00, 0x00 },	/* 650   700      0.6   */
	{ 0x6, 0x7F, 0x37, 0x00, 0x08 },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

struct icl_mg_phy_ddi_buf_trans {
	u32 cri_txdeemph_override_5_0;
	u32 cri_txdeemph_override_11_6;
	u32 cri_txdeemph_override_17_12;
};

static const struct icl_mg_phy_ddi_buf_trans icl_mg_phy_ddi_translations[] = {
				/* Voltage swing  pre-emphasis */
	{ 0x0, 0x1B, 0x00 },	/* 0              0   */
	{ 0x0, 0x23, 0x08 },	/* 0              1   */
	{ 0x0, 0x2D, 0x12 },	/* 0              2   */
	{ 0x0, 0x00, 0x00 },	/* 0              3   */
	{ 0x0, 0x23, 0x00 },	/* 1              0   */
	{ 0x0, 0x2B, 0x09 },	/* 1              1   */
	{ 0x0, 0x2E, 0x11 },	/* 1              2   */
	{ 0x0, 0x2F, 0x00 },	/* 2              0   */
	{ 0x0, 0x33, 0x0C },	/* 2              1   */
	{ 0x0, 0x00, 0x00 },	/* 3              0   */
};

struct tgl_dkl_phy_ddi_buf_trans {
	u32 dkl_vswing_control;
	u32 dkl_preshoot_control;
	u32 dkl_de_emphasis_control;
};

static const struct tgl_dkl_phy_ddi_buf_trans tgl_dkl_phy_dp_ddi_trans[] = {
				/* VS	pre-emp	Non-trans mV	Pre-emph dB */
	{ 0x7, 0x0, 0x00 },	/* 0	0	400mV		0 dB */
	{ 0x5, 0x0, 0x03 },	/* 0	1	400mV		3.5 dB */
	{ 0x2, 0x0, 0x0b },	/* 0	2	400mV		6 dB */
	{ 0x0, 0x0, 0x19 },	/* 0	3	400mV		9.5 dB */
	{ 0x5, 0x0, 0x00 },	/* 1	0	600mV		0 dB */
	{ 0x2, 0x0, 0x03 },	/* 1	1	600mV		3.5 dB */
	{ 0x0, 0x0, 0x14 },	/* 1	2	600mV		6 dB */
	{ 0x2, 0x0, 0x00 },	/* 2	0	800mV		0 dB */
	{ 0x0, 0x0, 0x0B },	/* 2	1	800mV		3.5 dB */
	{ 0x0, 0x0, 0x00 },	/* 3	0	1200mV		0 dB HDMI default */
};

static const struct tgl_dkl_phy_ddi_buf_trans tgl_dkl_phy_hdmi_ddi_trans[] = {
				/* HDMI Preset	VS	Pre-emph */
	{ 0x7, 0x0, 0x0 },	/* 1		400mV	0dB */
	{ 0x6, 0x0, 0x0 },	/* 2		500mV	0dB */
	{ 0x4, 0x0, 0x0 },	/* 3		650mV	0dB */
	{ 0x2, 0x0, 0x0 },	/* 4		800mV	0dB */
	{ 0x0, 0x0, 0x0 },	/* 5		1000mV	0dB */
	{ 0x0, 0x0, 0x5 },	/* 6		Full	-1.5 dB */
	{ 0x0, 0x0, 0x6 },	/* 7		Full	-1.8 dB */
	{ 0x0, 0x0, 0x7 },	/* 8		Full	-2 dB */
	{ 0x0, 0x0, 0x8 },	/* 9		Full	-2.5 dB */
	{ 0x0, 0x0, 0xA },	/* 10		Full	-3 dB */
};

static const struct cnl_ddi_buf_trans tgl_combo_phy_ddi_translations_dp_hbr[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x32, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x4F, 0x37, 0x00, 0x08 },	/* 350   500      3.1   */
	{ 0xC, 0x71, 0x2F, 0x00, 0x10 },	/* 350   700      6.0   */
	{ 0x6, 0x7D, 0x2B, 0x00, 0x14 },	/* 350   900      8.2   */
	{ 0xA, 0x4C, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x73, 0x34, 0x00, 0x0B },	/* 500   700      2.9   */
	{ 0x6, 0x7F, 0x2F, 0x00, 0x10 },	/* 500   900      5.1   */
	{ 0xC, 0x6C, 0x3C, 0x00, 0x03 },	/* 650   700      0.6   */
	{ 0x6, 0x7F, 0x35, 0x00, 0x0A },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

static const struct cnl_ddi_buf_trans tgl_combo_phy_ddi_translations_dp_hbr2[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x35, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x4F, 0x37, 0x00, 0x08 },	/* 350   500      3.1   */
	{ 0xC, 0x63, 0x2F, 0x00, 0x10 },	/* 350   700      6.0   */
	{ 0x6, 0x7F, 0x2B, 0x00, 0x14 },	/* 350   900      8.2   */
	{ 0xA, 0x47, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x63, 0x34, 0x00, 0x0B },	/* 500   700      2.9   */
	{ 0x6, 0x7F, 0x2F, 0x00, 0x10 },	/* 500   900      5.1   */
	{ 0xC, 0x61, 0x3C, 0x00, 0x03 },	/* 650   700      0.6   */
	{ 0x6, 0x7B, 0x35, 0x00, 0x0A },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

static const struct ddi_buf_trans *
bdw_get_buf_trans_edp(struct drm_i915_private *dev_priv, int *n_entries)
{
	if (dev_priv->vbt.edp.low_vswing) {
		*n_entries = ARRAY_SIZE(bdw_ddi_translations_edp);
		return bdw_ddi_translations_edp;
	} else {
		*n_entries = ARRAY_SIZE(bdw_ddi_translations_dp);
		return bdw_ddi_translations_dp;
	}
}

static const struct ddi_buf_trans *
skl_get_buf_trans_dp(struct drm_i915_private *dev_priv, int *n_entries)
{
	if (IS_SKL_ULX(dev_priv)) {
		*n_entries = ARRAY_SIZE(skl_y_ddi_translations_dp);
		return skl_y_ddi_translations_dp;
	} else if (IS_SKL_ULT(dev_priv)) {
		*n_entries = ARRAY_SIZE(skl_u_ddi_translations_dp);
		return skl_u_ddi_translations_dp;
	} else {
		*n_entries = ARRAY_SIZE(skl_ddi_translations_dp);
		return skl_ddi_translations_dp;
	}
}

static const struct ddi_buf_trans *
kbl_get_buf_trans_dp(struct drm_i915_private *dev_priv, int *n_entries)
{
	if (IS_KBL_ULX(dev_priv) || IS_CFL_ULX(dev_priv)) {
		*n_entries = ARRAY_SIZE(kbl_y_ddi_translations_dp);
		return kbl_y_ddi_translations_dp;
	} else if (IS_KBL_ULT(dev_priv) || IS_CFL_ULT(dev_priv)) {
		*n_entries = ARRAY_SIZE(kbl_u_ddi_translations_dp);
		return kbl_u_ddi_translations_dp;
	} else {
		*n_entries = ARRAY_SIZE(kbl_ddi_translations_dp);
		return kbl_ddi_translations_dp;
	}
}

static const struct ddi_buf_trans *
skl_get_buf_trans_edp(struct drm_i915_private *dev_priv, int *n_entries)
{
	if (dev_priv->vbt.edp.low_vswing) {
		if (IS_SKL_ULX(dev_priv) || IS_KBL_ULX(dev_priv) ||
		    IS_CFL_ULX(dev_priv)) {
			*n_entries = ARRAY_SIZE(skl_y_ddi_translations_edp);
			return skl_y_ddi_translations_edp;
		} else if (IS_SKL_ULT(dev_priv) || IS_KBL_ULT(dev_priv) ||
			   IS_CFL_ULT(dev_priv)) {
			*n_entries = ARRAY_SIZE(skl_u_ddi_translations_edp);
			return skl_u_ddi_translations_edp;
		} else {
			*n_entries = ARRAY_SIZE(skl_ddi_translations_edp);
			return skl_ddi_translations_edp;
		}
	}

	if (IS_KABYLAKE(dev_priv) || IS_COFFEELAKE(dev_priv))
		return kbl_get_buf_trans_dp(dev_priv, n_entries);
	else
		return skl_get_buf_trans_dp(dev_priv, n_entries);
}

static const struct ddi_buf_trans *
skl_get_buf_trans_hdmi(struct drm_i915_private *dev_priv, int *n_entries)
{
	if (IS_SKL_ULX(dev_priv) || IS_KBL_ULX(dev_priv) ||
	    IS_CFL_ULX(dev_priv)) {
		*n_entries = ARRAY_SIZE(skl_y_ddi_translations_hdmi);
		return skl_y_ddi_translations_hdmi;
	} else {
		*n_entries = ARRAY_SIZE(skl_ddi_translations_hdmi);
		return skl_ddi_translations_hdmi;
	}
}

static int skl_buf_trans_num_entries(enum port port, int n_entries)
{
	/* Only DDIA and DDIE can select the 10th register with DP */
	if (port == PORT_A || port == PORT_E)
		return min(n_entries, 10);
	else
		return min(n_entries, 9);
}

static const struct ddi_buf_trans *
intel_ddi_get_buf_trans_dp(struct drm_i915_private *dev_priv,
			   enum port port, int *n_entries)
{
	if (IS_KABYLAKE(dev_priv) || IS_COFFEELAKE(dev_priv)) {
		const struct ddi_buf_trans *ddi_translations =
			kbl_get_buf_trans_dp(dev_priv, n_entries);
		*n_entries = skl_buf_trans_num_entries(port, *n_entries);
		return ddi_translations;
	} else if (IS_SKYLAKE(dev_priv)) {
		const struct ddi_buf_trans *ddi_translations =
			skl_get_buf_trans_dp(dev_priv, n_entries);
		*n_entries = skl_buf_trans_num_entries(port, *n_entries);
		return ddi_translations;
	} else if (IS_BROADWELL(dev_priv)) {
		*n_entries = ARRAY_SIZE(bdw_ddi_translations_dp);
		return  bdw_ddi_translations_dp;
	} else if (IS_HASWELL(dev_priv)) {
		*n_entries = ARRAY_SIZE(hsw_ddi_translations_dp);
		return hsw_ddi_translations_dp;
	}

	*n_entries = 0;
	return NULL;
}

static const struct ddi_buf_trans *
intel_ddi_get_buf_trans_edp(struct drm_i915_private *dev_priv,
			    enum port port, int *n_entries)
{
	if (IS_GEN9_BC(dev_priv)) {
		const struct ddi_buf_trans *ddi_translations =
			skl_get_buf_trans_edp(dev_priv, n_entries);
		*n_entries = skl_buf_trans_num_entries(port, *n_entries);
		return ddi_translations;
	} else if (IS_BROADWELL(dev_priv)) {
		return bdw_get_buf_trans_edp(dev_priv, n_entries);
	} else if (IS_HASWELL(dev_priv)) {
		*n_entries = ARRAY_SIZE(hsw_ddi_translations_dp);
		return hsw_ddi_translations_dp;
	}

	*n_entries = 0;
	return NULL;
}

static const struct ddi_buf_trans *
intel_ddi_get_buf_trans_fdi(struct drm_i915_private *dev_priv,
			    int *n_entries)
{
	if (IS_BROADWELL(dev_priv)) {
		*n_entries = ARRAY_SIZE(bdw_ddi_translations_fdi);
		return bdw_ddi_translations_fdi;
	} else if (IS_HASWELL(dev_priv)) {
		*n_entries = ARRAY_SIZE(hsw_ddi_translations_fdi);
		return hsw_ddi_translations_fdi;
	}

	*n_entries = 0;
	return NULL;
}

static const struct ddi_buf_trans *
intel_ddi_get_buf_trans_hdmi(struct drm_i915_private *dev_priv,
			     int *n_entries)
{
	if (IS_GEN9_BC(dev_priv)) {
		return skl_get_buf_trans_hdmi(dev_priv, n_entries);
	} else if (IS_BROADWELL(dev_priv)) {
		*n_entries = ARRAY_SIZE(bdw_ddi_translations_hdmi);
		return bdw_ddi_translations_hdmi;
	} else if (IS_HASWELL(dev_priv)) {
		*n_entries = ARRAY_SIZE(hsw_ddi_translations_hdmi);
		return hsw_ddi_translations_hdmi;
	}

	*n_entries = 0;
	return NULL;
}

static const struct bxt_ddi_buf_trans *
bxt_get_buf_trans_dp(struct drm_i915_private *dev_priv, int *n_entries)
{
	*n_entries = ARRAY_SIZE(bxt_ddi_translations_dp);
	return bxt_ddi_translations_dp;
}

static const struct bxt_ddi_buf_trans *
bxt_get_buf_trans_edp(struct drm_i915_private *dev_priv, int *n_entries)
{
	if (dev_priv->vbt.edp.low_vswing) {
		*n_entries = ARRAY_SIZE(bxt_ddi_translations_edp);
		return bxt_ddi_translations_edp;
	}

	return bxt_get_buf_trans_dp(dev_priv, n_entries);
}

static const struct bxt_ddi_buf_trans *
bxt_get_buf_trans_hdmi(struct drm_i915_private *dev_priv, int *n_entries)
{
	*n_entries = ARRAY_SIZE(bxt_ddi_translations_hdmi);
	return bxt_ddi_translations_hdmi;
}

static const struct cnl_ddi_buf_trans *
cnl_get_buf_trans_hdmi(struct drm_i915_private *dev_priv, int *n_entries)
{
	u32 voltage = intel_de_read(dev_priv, CNL_PORT_COMP_DW3) & VOLTAGE_INFO_MASK;

	if (voltage == VOLTAGE_INFO_0_85V) {
		*n_entries = ARRAY_SIZE(cnl_ddi_translations_hdmi_0_85V);
		return cnl_ddi_translations_hdmi_0_85V;
	} else if (voltage == VOLTAGE_INFO_0_95V) {
		*n_entries = ARRAY_SIZE(cnl_ddi_translations_hdmi_0_95V);
		return cnl_ddi_translations_hdmi_0_95V;
	} else if (voltage == VOLTAGE_INFO_1_05V) {
		*n_entries = ARRAY_SIZE(cnl_ddi_translations_hdmi_1_05V);
		return cnl_ddi_translations_hdmi_1_05V;
	} else {
		*n_entries = 1; /* shut up gcc */
		MISSING_CASE(voltage);
	}
	return NULL;
}

static const struct cnl_ddi_buf_trans *
cnl_get_buf_trans_dp(struct drm_i915_private *dev_priv, int *n_entries)
{
	u32 voltage = intel_de_read(dev_priv, CNL_PORT_COMP_DW3) & VOLTAGE_INFO_MASK;

	if (voltage == VOLTAGE_INFO_0_85V) {
		*n_entries = ARRAY_SIZE(cnl_ddi_translations_dp_0_85V);
		return cnl_ddi_translations_dp_0_85V;
	} else if (voltage == VOLTAGE_INFO_0_95V) {
		*n_entries = ARRAY_SIZE(cnl_ddi_translations_dp_0_95V);
		return cnl_ddi_translations_dp_0_95V;
	} else if (voltage == VOLTAGE_INFO_1_05V) {
		*n_entries = ARRAY_SIZE(cnl_ddi_translations_dp_1_05V);
		return cnl_ddi_translations_dp_1_05V;
	} else {
		*n_entries = 1; /* shut up gcc */
		MISSING_CASE(voltage);
	}
	return NULL;
}

static const struct cnl_ddi_buf_trans *
cnl_get_buf_trans_edp(struct drm_i915_private *dev_priv, int *n_entries)
{
	u32 voltage = intel_de_read(dev_priv, CNL_PORT_COMP_DW3) & VOLTAGE_INFO_MASK;

	if (dev_priv->vbt.edp.low_vswing) {
		if (voltage == VOLTAGE_INFO_0_85V) {
			*n_entries = ARRAY_SIZE(cnl_ddi_translations_edp_0_85V);
			return cnl_ddi_translations_edp_0_85V;
		} else if (voltage == VOLTAGE_INFO_0_95V) {
			*n_entries = ARRAY_SIZE(cnl_ddi_translations_edp_0_95V);
			return cnl_ddi_translations_edp_0_95V;
		} else if (voltage == VOLTAGE_INFO_1_05V) {
			*n_entries = ARRAY_SIZE(cnl_ddi_translations_edp_1_05V);
			return cnl_ddi_translations_edp_1_05V;
		} else {
			*n_entries = 1; /* shut up gcc */
			MISSING_CASE(voltage);
		}
		return NULL;
	} else {
		return cnl_get_buf_trans_dp(dev_priv, n_entries);
	}
}

static const struct cnl_ddi_buf_trans *
icl_get_combo_buf_trans(struct drm_i915_private *dev_priv, int type, int rate,
			int *n_entries)
{
	if (type == INTEL_OUTPUT_HDMI) {
		*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_hdmi);
		return icl_combo_phy_ddi_translations_hdmi;
	} else if (rate > 540000 && type == INTEL_OUTPUT_EDP) {
		*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_edp_hbr3);
		return icl_combo_phy_ddi_translations_edp_hbr3;
	} else if (type == INTEL_OUTPUT_EDP && dev_priv->vbt.edp.low_vswing) {
		*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_edp_hbr2);
		return icl_combo_phy_ddi_translations_edp_hbr2;
	}

	*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_dp_hbr2);
	return icl_combo_phy_ddi_translations_dp_hbr2;
}

static const struct cnl_ddi_buf_trans *
ehl_get_combo_buf_trans(struct drm_i915_private *dev_priv, int type, int rate,
			int *n_entries)
{
	if (type != INTEL_OUTPUT_HDMI && type != INTEL_OUTPUT_EDP &&
	    rate > 270000) {
		*n_entries = ARRAY_SIZE(ehl_combo_phy_ddi_translations_hbr2_hbr3);
		return ehl_combo_phy_ddi_translations_hbr2_hbr3;
	}

	return icl_get_combo_buf_trans(dev_priv, type, rate, n_entries);
}

static const struct cnl_ddi_buf_trans *
tgl_get_combo_buf_trans(struct drm_i915_private *dev_priv, int type, int rate,
			int *n_entries)
{
	if (type == INTEL_OUTPUT_HDMI || type == INTEL_OUTPUT_EDP) {
		return icl_get_combo_buf_trans(dev_priv, type, rate, n_entries);
	} else if (rate > 270000) {
		*n_entries = ARRAY_SIZE(tgl_combo_phy_ddi_translations_dp_hbr2);
		return tgl_combo_phy_ddi_translations_dp_hbr2;
	}

	*n_entries = ARRAY_SIZE(tgl_combo_phy_ddi_translations_dp_hbr);
	return tgl_combo_phy_ddi_translations_dp_hbr;
}

static int intel_ddi_hdmi_level(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int n_entries, level, default_entry;
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);

	if (INTEL_GEN(dev_priv) >= 12) {
		if (intel_phy_is_combo(dev_priv, phy))
			tgl_get_combo_buf_trans(dev_priv, INTEL_OUTPUT_HDMI,
						0, &n_entries);
		else
			n_entries = ARRAY_SIZE(tgl_dkl_phy_hdmi_ddi_trans);
		default_entry = n_entries - 1;
	} else if (INTEL_GEN(dev_priv) == 11) {
		if (intel_phy_is_combo(dev_priv, phy))
			icl_get_combo_buf_trans(dev_priv, INTEL_OUTPUT_HDMI,
						0, &n_entries);
		else
			n_entries = ARRAY_SIZE(icl_mg_phy_ddi_translations);
		default_entry = n_entries - 1;
	} else if (IS_CANNONLAKE(dev_priv)) {
		cnl_get_buf_trans_hdmi(dev_priv, &n_entries);
		default_entry = n_entries - 1;
	} else if (IS_GEN9_LP(dev_priv)) {
		bxt_get_buf_trans_hdmi(dev_priv, &n_entries);
		default_entry = n_entries - 1;
	} else if (IS_GEN9_BC(dev_priv)) {
		intel_ddi_get_buf_trans_hdmi(dev_priv, &n_entries);
		default_entry = 8;
	} else if (IS_BROADWELL(dev_priv)) {
		intel_ddi_get_buf_trans_hdmi(dev_priv, &n_entries);
		default_entry = 7;
	} else if (IS_HASWELL(dev_priv)) {
		intel_ddi_get_buf_trans_hdmi(dev_priv, &n_entries);
		default_entry = 6;
	} else {
		drm_WARN(&dev_priv->drm, 1, "ddi translation table missing\n");
		return 0;
	}

	if (drm_WARN_ON_ONCE(&dev_priv->drm, n_entries == 0))
		return 0;

	level = intel_bios_hdmi_level_shift(encoder);
	if (level < 0)
		level = default_entry;

	if (drm_WARN_ON_ONCE(&dev_priv->drm, level >= n_entries))
		level = n_entries - 1;

	return level;
}

/*
 * Starting with Haswell, DDI port buffers must be programmed with correct
 * values in advance. This function programs the correct values for
 * DP/eDP/FDI use cases.
 */
static void intel_prepare_dp_ddi_buffers(struct intel_encoder *encoder,
					 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 iboost_bit = 0;
	int i, n_entries;
	enum port port = encoder->port;
	const struct ddi_buf_trans *ddi_translations;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_ANALOG))
		ddi_translations = intel_ddi_get_buf_trans_fdi(dev_priv,
							       &n_entries);
	else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		ddi_translations = intel_ddi_get_buf_trans_edp(dev_priv, port,
							       &n_entries);
	else
		ddi_translations = intel_ddi_get_buf_trans_dp(dev_priv, port,
							      &n_entries);

	/* If we're boosting the current, set bit 31 of trans1 */
	if (IS_GEN9_BC(dev_priv) && intel_bios_dp_boost_level(encoder))
		iboost_bit = DDI_BUF_BALANCE_LEG_ENABLE;

	for (i = 0; i < n_entries; i++) {
		intel_de_write(dev_priv, DDI_BUF_TRANS_LO(port, i),
			       ddi_translations[i].trans1 | iboost_bit);
		intel_de_write(dev_priv, DDI_BUF_TRANS_HI(port, i),
			       ddi_translations[i].trans2);
	}
}

/*
 * Starting with Haswell, DDI port buffers must be programmed with correct
 * values in advance. This function programs the correct values for
 * HDMI/DVI use cases.
 */
static void intel_prepare_hdmi_ddi_buffers(struct intel_encoder *encoder,
					   int level)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 iboost_bit = 0;
	int n_entries;
	enum port port = encoder->port;
	const struct ddi_buf_trans *ddi_translations;

	ddi_translations = intel_ddi_get_buf_trans_hdmi(dev_priv, &n_entries);

	if (drm_WARN_ON_ONCE(&dev_priv->drm, !ddi_translations))
		return;
	if (drm_WARN_ON_ONCE(&dev_priv->drm, level >= n_entries))
		level = n_entries - 1;

	/* If we're boosting the current, set bit 31 of trans1 */
	if (IS_GEN9_BC(dev_priv) && intel_bios_hdmi_boost_level(encoder))
		iboost_bit = DDI_BUF_BALANCE_LEG_ENABLE;

	/* Entry 9 is for HDMI: */
	intel_de_write(dev_priv, DDI_BUF_TRANS_LO(port, 9),
		       ddi_translations[level].trans1 | iboost_bit);
	intel_de_write(dev_priv, DDI_BUF_TRANS_HI(port, 9),
		       ddi_translations[level].trans2);
}

static void intel_wait_ddi_buf_idle(struct drm_i915_private *dev_priv,
				    enum port port)
{
	i915_reg_t reg = DDI_BUF_CTL(port);
	int i;

	for (i = 0; i < 16; i++) {
		udelay(1);
		if (intel_de_read(dev_priv, reg) & DDI_BUF_IS_IDLE)
			return;
	}
	DRM_ERROR("Timeout waiting for DDI BUF %c idle bit\n", port_name(port));
}

static u32 hsw_pll_to_ddi_pll_sel(const struct intel_shared_dpll *pll)
{
	switch (pll->info->id) {
	case DPLL_ID_WRPLL1:
		return PORT_CLK_SEL_WRPLL1;
	case DPLL_ID_WRPLL2:
		return PORT_CLK_SEL_WRPLL2;
	case DPLL_ID_SPLL:
		return PORT_CLK_SEL_SPLL;
	case DPLL_ID_LCPLL_810:
		return PORT_CLK_SEL_LCPLL_810;
	case DPLL_ID_LCPLL_1350:
		return PORT_CLK_SEL_LCPLL_1350;
	case DPLL_ID_LCPLL_2700:
		return PORT_CLK_SEL_LCPLL_2700;
	default:
		MISSING_CASE(pll->info->id);
		return PORT_CLK_SEL_NONE;
	}
}

static u32 icl_pll_to_ddi_clk_sel(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	int clock = crtc_state->port_clock;
	const enum intel_dpll_id id = pll->info->id;

	switch (id) {
	default:
		/*
		 * DPLL_ID_ICL_DPLL0 and DPLL_ID_ICL_DPLL1 should not be used
		 * here, so do warn if this get passed in
		 */
		MISSING_CASE(id);
		return DDI_CLK_SEL_NONE;
	case DPLL_ID_ICL_TBTPLL:
		switch (clock) {
		case 162000:
			return DDI_CLK_SEL_TBT_162;
		case 270000:
			return DDI_CLK_SEL_TBT_270;
		case 540000:
			return DDI_CLK_SEL_TBT_540;
		case 810000:
			return DDI_CLK_SEL_TBT_810;
		default:
			MISSING_CASE(clock);
			return DDI_CLK_SEL_NONE;
		}
	case DPLL_ID_ICL_MGPLL1:
	case DPLL_ID_ICL_MGPLL2:
	case DPLL_ID_ICL_MGPLL3:
	case DPLL_ID_ICL_MGPLL4:
	case DPLL_ID_TGL_MGPLL5:
	case DPLL_ID_TGL_MGPLL6:
		return DDI_CLK_SEL_MG;
	}
}

/* Starting with Haswell, different DDI ports can work in FDI mode for
 * connection to the PCH-located connectors. For this, it is necessary to train
 * both the DDI port and PCH receiver for the desired DDI buffer settings.
 *
 * The recommended port to work in FDI mode is DDI E, which we use here. Also,
 * please note that when FDI mode is active on DDI E, it shares 2 lines with
 * DDI A (which is used for eDP)
 */

void hsw_fdi_link_train(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 temp, i, rx_ctl_val, ddi_pll_sel;

	intel_prepare_dp_ddi_buffers(encoder, crtc_state);

	/* Set the FDI_RX_MISC pwrdn lanes and the 2 workarounds listed at the
	 * mode set "sequence for CRT port" document:
	 * - TP1 to TP2 time with the default value
	 * - FDI delay to 90h
	 *
	 * WaFDIAutoLinkSetTimingOverrride:hsw
	 */
	intel_de_write(dev_priv, FDI_RX_MISC(PIPE_A),
		       FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2) | FDI_RX_TP1_TO_TP2_48 | FDI_RX_FDI_DELAY_90);

	/* Enable the PCH Receiver FDI PLL */
	rx_ctl_val = dev_priv->fdi_rx_config | FDI_RX_ENHANCE_FRAME_ENABLE |
		     FDI_RX_PLL_ENABLE |
		     FDI_DP_PORT_WIDTH(crtc_state->fdi_lanes);
	intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), rx_ctl_val);
	intel_de_posting_read(dev_priv, FDI_RX_CTL(PIPE_A));
	udelay(220);

	/* Switch from Rawclk to PCDclk */
	rx_ctl_val |= FDI_PCDCLK;
	intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), rx_ctl_val);

	/* Configure Port Clock Select */
	ddi_pll_sel = hsw_pll_to_ddi_pll_sel(crtc_state->shared_dpll);
	intel_de_write(dev_priv, PORT_CLK_SEL(PORT_E), ddi_pll_sel);
	drm_WARN_ON(&dev_priv->drm, ddi_pll_sel != PORT_CLK_SEL_SPLL);

	/* Start the training iterating through available voltages and emphasis,
	 * testing each value twice. */
	for (i = 0; i < ARRAY_SIZE(hsw_ddi_translations_fdi) * 2; i++) {
		/* Configure DP_TP_CTL with auto-training */
		intel_de_write(dev_priv, DP_TP_CTL(PORT_E),
			       DP_TP_CTL_FDI_AUTOTRAIN | DP_TP_CTL_ENHANCED_FRAME_ENABLE | DP_TP_CTL_LINK_TRAIN_PAT1 | DP_TP_CTL_ENABLE);

		/* Configure and enable DDI_BUF_CTL for DDI E with next voltage.
		 * DDI E does not support port reversal, the functionality is
		 * achieved on the PCH side in FDI_RX_CTL, so no need to set the
		 * port reversal bit */
		intel_de_write(dev_priv, DDI_BUF_CTL(PORT_E),
			       DDI_BUF_CTL_ENABLE | ((crtc_state->fdi_lanes - 1) << 1) | DDI_BUF_TRANS_SELECT(i / 2));
		intel_de_posting_read(dev_priv, DDI_BUF_CTL(PORT_E));

		udelay(600);

		/* Program PCH FDI Receiver TU */
		intel_de_write(dev_priv, FDI_RX_TUSIZE1(PIPE_A), TU_SIZE(64));

		/* Enable PCH FDI Receiver with auto-training */
		rx_ctl_val |= FDI_RX_ENABLE | FDI_LINK_TRAIN_AUTO;
		intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), rx_ctl_val);
		intel_de_posting_read(dev_priv, FDI_RX_CTL(PIPE_A));

		/* Wait for FDI receiver lane calibration */
		udelay(30);

		/* Unset FDI_RX_MISC pwrdn lanes */
		temp = intel_de_read(dev_priv, FDI_RX_MISC(PIPE_A));
		temp &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
		intel_de_write(dev_priv, FDI_RX_MISC(PIPE_A), temp);
		intel_de_posting_read(dev_priv, FDI_RX_MISC(PIPE_A));

		/* Wait for FDI auto training time */
		udelay(5);

		temp = intel_de_read(dev_priv, DP_TP_STATUS(PORT_E));
		if (temp & DP_TP_STATUS_AUTOTRAIN_DONE) {
			DRM_DEBUG_KMS("FDI link training done on step %d\n", i);
			break;
		}

		/*
		 * Leave things enabled even if we failed to train FDI.
		 * Results in less fireworks from the state checker.
		 */
		if (i == ARRAY_SIZE(hsw_ddi_translations_fdi) * 2 - 1) {
			DRM_ERROR("FDI link training failed!\n");
			break;
		}

		rx_ctl_val &= ~FDI_RX_ENABLE;
		intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), rx_ctl_val);
		intel_de_posting_read(dev_priv, FDI_RX_CTL(PIPE_A));

		temp = intel_de_read(dev_priv, DDI_BUF_CTL(PORT_E));
		temp &= ~DDI_BUF_CTL_ENABLE;
		intel_de_write(dev_priv, DDI_BUF_CTL(PORT_E), temp);
		intel_de_posting_read(dev_priv, DDI_BUF_CTL(PORT_E));

		/* Disable DP_TP_CTL and FDI_RX_CTL and retry */
		temp = intel_de_read(dev_priv, DP_TP_CTL(PORT_E));
		temp &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
		temp |= DP_TP_CTL_LINK_TRAIN_PAT1;
		intel_de_write(dev_priv, DP_TP_CTL(PORT_E), temp);
		intel_de_posting_read(dev_priv, DP_TP_CTL(PORT_E));

		intel_wait_ddi_buf_idle(dev_priv, PORT_E);

		/* Reset FDI_RX_MISC pwrdn lanes */
		temp = intel_de_read(dev_priv, FDI_RX_MISC(PIPE_A));
		temp &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
		temp |= FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2);
		intel_de_write(dev_priv, FDI_RX_MISC(PIPE_A), temp);
		intel_de_posting_read(dev_priv, FDI_RX_MISC(PIPE_A));
	}

	/* Enable normal pixel sending for FDI */
	intel_de_write(dev_priv, DP_TP_CTL(PORT_E),
		       DP_TP_CTL_FDI_AUTOTRAIN | DP_TP_CTL_LINK_TRAIN_NORMAL | DP_TP_CTL_ENHANCED_FRAME_ENABLE | DP_TP_CTL_ENABLE);
}

static void intel_ddi_init_dp_buf_reg(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_digital_port *intel_dig_port =
		enc_to_dig_port(encoder);

	intel_dp->DP = intel_dig_port->saved_port_bits |
		DDI_BUF_CTL_ENABLE | DDI_BUF_TRANS_SELECT(0);
	intel_dp->DP |= DDI_PORT_WIDTH(intel_dp->lane_count);
}

static struct intel_encoder *
intel_ddi_get_crtc_encoder(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct intel_encoder *encoder, *ret = NULL;
	int num_encoders = 0;

	for_each_encoder_on_crtc(dev, &crtc->base, encoder) {
		ret = encoder;
		num_encoders++;
	}

	if (num_encoders != 1)
		drm_WARN(dev, 1, "%d encoders on crtc for pipe %c\n",
			 num_encoders,
			 pipe_name(crtc->pipe));

	BUG_ON(ret == NULL);
	return ret;
}

static int icl_calc_tbt_pll_link(struct drm_i915_private *dev_priv,
				 enum port port)
{
	u32 val = intel_de_read(dev_priv, DDI_CLK_SEL(port)) & DDI_CLK_SEL_MASK;

	switch (val) {
	case DDI_CLK_SEL_NONE:
		return 0;
	case DDI_CLK_SEL_TBT_162:
		return 162000;
	case DDI_CLK_SEL_TBT_270:
		return 270000;
	case DDI_CLK_SEL_TBT_540:
		return 540000;
	case DDI_CLK_SEL_TBT_810:
		return 810000;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static void ddi_dotclock_get(struct intel_crtc_state *pipe_config)
{
	int dotclock;

	if (pipe_config->has_pch_encoder)
		dotclock = intel_dotclock_calculate(pipe_config->port_clock,
						    &pipe_config->fdi_m_n);
	else if (intel_crtc_has_dp_encoder(pipe_config))
		dotclock = intel_dotclock_calculate(pipe_config->port_clock,
						    &pipe_config->dp_m_n);
	else if (pipe_config->has_hdmi_sink && pipe_config->pipe_bpp > 24)
		dotclock = pipe_config->port_clock * 24 / pipe_config->pipe_bpp;
	else
		dotclock = pipe_config->port_clock;

	if (pipe_config->output_format == INTEL_OUTPUT_FORMAT_YCBCR420 &&
	    !intel_crtc_has_dp_encoder(pipe_config))
		dotclock *= 2;

	if (pipe_config->pixel_multiplier)
		dotclock /= pipe_config->pixel_multiplier;

	pipe_config->hw.adjusted_mode.crtc_clock = dotclock;
}

static void intel_ddi_clock_get(struct intel_encoder *encoder,
				struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);

	if (intel_phy_is_tc(dev_priv, phy) &&
	    intel_get_shared_dpll_id(dev_priv, pipe_config->shared_dpll) ==
	    DPLL_ID_ICL_TBTPLL)
		pipe_config->port_clock = icl_calc_tbt_pll_link(dev_priv,
								encoder->port);
	else
		pipe_config->port_clock =
			intel_dpll_get_freq(dev_priv, pipe_config->shared_dpll);

	ddi_dotclock_get(pipe_config);
}

void intel_ddi_set_dp_msa(const struct intel_crtc_state *crtc_state,
			  const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 temp;

	if (!intel_crtc_has_dp_encoder(crtc_state))
		return;

	drm_WARN_ON(&dev_priv->drm, transcoder_is_dsi(cpu_transcoder));

	temp = DP_MSA_MISC_SYNC_CLOCK;

	switch (crtc_state->pipe_bpp) {
	case 18:
		temp |= DP_MSA_MISC_6_BPC;
		break;
	case 24:
		temp |= DP_MSA_MISC_8_BPC;
		break;
	case 30:
		temp |= DP_MSA_MISC_10_BPC;
		break;
	case 36:
		temp |= DP_MSA_MISC_12_BPC;
		break;
	default:
		MISSING_CASE(crtc_state->pipe_bpp);
		break;
	}

	/* nonsense combination */
	drm_WARN_ON(&dev_priv->drm, crtc_state->limited_color_range &&
		    crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB);

	if (crtc_state->limited_color_range)
		temp |= DP_MSA_MISC_COLOR_CEA_RGB;

	/*
	 * As per DP 1.2 spec section 2.3.4.3 while sending
	 * YCBCR 444 signals we should program MSA MISC1/0 fields with
	 * colorspace information.
	 */
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444)
		temp |= DP_MSA_MISC_COLOR_YCBCR_444_BT709;

	/*
	 * As per DP 1.4a spec section 2.2.4.3 [MSA Field for Indication
	 * of Color Encoding Format and Content Color Gamut] while sending
	 * YCBCR 420, HDR BT.2020 signals we should program MSA MISC1 fields
	 * which indicate VSC SDP for the Pixel Encoding/Colorimetry Format.
	 */
	if (intel_dp_needs_vsc_sdp(crtc_state, conn_state))
		temp |= DP_MSA_MISC_COLOR_VSC_SDP;

	intel_de_write(dev_priv, TRANS_MSA_MISC(cpu_transcoder), temp);
}

/*
 * Returns the TRANS_DDI_FUNC_CTL value based on CRTC state.
 *
 * Only intended to be used by intel_ddi_enable_transcoder_func() and
 * intel_ddi_config_transcoder_func().
 */
static u32
intel_ddi_transcoder_func_reg_val_get(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct intel_encoder *encoder = intel_ddi_get_crtc_encoder(crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum port port = encoder->port;
	u32 temp;

	/* Enable TRANS_DDI_FUNC_CTL for the pipe to work in HDMI mode */
	temp = TRANS_DDI_FUNC_ENABLE;
	if (INTEL_GEN(dev_priv) >= 12)
		temp |= TGL_TRANS_DDI_SELECT_PORT(port);
	else
		temp |= TRANS_DDI_SELECT_PORT(port);

	switch (crtc_state->pipe_bpp) {
	case 18:
		temp |= TRANS_DDI_BPC_6;
		break;
	case 24:
		temp |= TRANS_DDI_BPC_8;
		break;
	case 30:
		temp |= TRANS_DDI_BPC_10;
		break;
	case 36:
		temp |= TRANS_DDI_BPC_12;
		break;
	default:
		BUG();
	}

	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_PVSYNC)
		temp |= TRANS_DDI_PVSYNC;
	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_PHSYNC)
		temp |= TRANS_DDI_PHSYNC;

	if (cpu_transcoder == TRANSCODER_EDP) {
		switch (pipe) {
		case PIPE_A:
			/* On Haswell, can only use the always-on power well for
			 * eDP when not using the panel fitter, and when not
			 * using motion blur mitigation (which we don't
			 * support). */
			if (crtc_state->pch_pfit.force_thru)
				temp |= TRANS_DDI_EDP_INPUT_A_ONOFF;
			else
				temp |= TRANS_DDI_EDP_INPUT_A_ON;
			break;
		case PIPE_B:
			temp |= TRANS_DDI_EDP_INPUT_B_ONOFF;
			break;
		case PIPE_C:
			temp |= TRANS_DDI_EDP_INPUT_C_ONOFF;
			break;
		default:
			BUG();
			break;
		}
	}

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		if (crtc_state->has_hdmi_sink)
			temp |= TRANS_DDI_MODE_SELECT_HDMI;
		else
			temp |= TRANS_DDI_MODE_SELECT_DVI;

		if (crtc_state->hdmi_scrambling)
			temp |= TRANS_DDI_HDMI_SCRAMBLING;
		if (crtc_state->hdmi_high_tmds_clock_ratio)
			temp |= TRANS_DDI_HIGH_TMDS_CHAR_RATE;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_ANALOG)) {
		temp |= TRANS_DDI_MODE_SELECT_FDI;
		temp |= (crtc_state->fdi_lanes - 1) << 1;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST)) {
		temp |= TRANS_DDI_MODE_SELECT_DP_MST;
		temp |= DDI_PORT_WIDTH(crtc_state->lane_count);

		if (INTEL_GEN(dev_priv) >= 12) {
			enum transcoder master;

			master = crtc_state->mst_master_transcoder;
			drm_WARN_ON(&dev_priv->drm,
				    master == INVALID_TRANSCODER);
			temp |= TRANS_DDI_MST_TRANSPORT_SELECT(master);
		}
	} else {
		temp |= TRANS_DDI_MODE_SELECT_DP_SST;
		temp |= DDI_PORT_WIDTH(crtc_state->lane_count);
	}

	return temp;
}

void intel_ddi_enable_transcoder_func(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 temp;

	temp = intel_ddi_transcoder_func_reg_val_get(crtc_state);
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST))
		temp |= TRANS_DDI_DP_VC_PAYLOAD_ALLOC;
	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder), temp);
}

/*
 * Same as intel_ddi_enable_transcoder_func(), but it does not set the enable
 * bit.
 */
static void
intel_ddi_config_transcoder_func(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 temp;

	temp = intel_ddi_transcoder_func_reg_val_get(crtc_state);
	temp &= ~TRANS_DDI_FUNC_ENABLE;
	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder), temp);
}

void intel_ddi_disable_transcoder_func(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 val;

	val = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));
	val &= ~TRANS_DDI_FUNC_ENABLE;

	if (INTEL_GEN(dev_priv) >= 12) {
		if (!intel_dp_mst_is_master_trans(crtc_state)) {
			val &= ~(TGL_TRANS_DDI_PORT_MASK |
				 TRANS_DDI_MODE_SELECT_MASK);
		}
	} else {
		val &= ~(TRANS_DDI_PORT_MASK | TRANS_DDI_MODE_SELECT_MASK);
	}
	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder), val);

	if (dev_priv->quirks & QUIRK_INCREASE_DDI_DISABLED_TIME &&
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		DRM_DEBUG_KMS("Quirk Increase DDI disabled time\n");
		/* Quirk time at 100ms for reliable operation */
		msleep(100);
	}
}

int intel_ddi_toggle_hdcp_signalling(struct intel_encoder *intel_encoder,
				     bool enable)
{
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	intel_wakeref_t wakeref;
	enum pipe pipe = 0;
	int ret = 0;
	u32 tmp;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     intel_encoder->power_domain);
	if (drm_WARN_ON(dev, !wakeref))
		return -ENXIO;

	if (drm_WARN_ON(dev,
			!intel_encoder->get_hw_state(intel_encoder, &pipe))) {
		ret = -EIO;
		goto out;
	}

	tmp = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(pipe));
	if (enable)
		tmp |= TRANS_DDI_HDCP_SIGNALLING;
	else
		tmp &= ~TRANS_DDI_HDCP_SIGNALLING;
	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(pipe), tmp);
out:
	intel_display_power_put(dev_priv, intel_encoder->power_domain, wakeref);
	return ret;
}

bool intel_ddi_connector_get_hw_state(struct intel_connector *intel_connector)
{
	struct drm_device *dev = intel_connector->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_encoder *encoder = intel_attached_encoder(intel_connector);
	int type = intel_connector->base.connector_type;
	enum port port = encoder->port;
	enum transcoder cpu_transcoder;
	intel_wakeref_t wakeref;
	enum pipe pipe = 0;
	u32 tmp;
	bool ret;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	if (!encoder->get_hw_state(encoder, &pipe)) {
		ret = false;
		goto out;
	}

	if (HAS_TRANSCODER_EDP(dev_priv) && port == PORT_A)
		cpu_transcoder = TRANSCODER_EDP;
	else
		cpu_transcoder = (enum transcoder) pipe;

	tmp = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));

	switch (tmp & TRANS_DDI_MODE_SELECT_MASK) {
	case TRANS_DDI_MODE_SELECT_HDMI:
	case TRANS_DDI_MODE_SELECT_DVI:
		ret = type == DRM_MODE_CONNECTOR_HDMIA;
		break;

	case TRANS_DDI_MODE_SELECT_DP_SST:
		ret = type == DRM_MODE_CONNECTOR_eDP ||
		      type == DRM_MODE_CONNECTOR_DisplayPort;
		break;

	case TRANS_DDI_MODE_SELECT_DP_MST:
		/* if the transcoder is in MST state then
		 * connector isn't connected */
		ret = false;
		break;

	case TRANS_DDI_MODE_SELECT_FDI:
		ret = type == DRM_MODE_CONNECTOR_VGA;
		break;

	default:
		ret = false;
		break;
	}

out:
	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);

	return ret;
}

static void intel_ddi_get_encoder_pipes(struct intel_encoder *encoder,
					u8 *pipe_mask, bool *is_dp_mst)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = encoder->port;
	intel_wakeref_t wakeref;
	enum pipe p;
	u32 tmp;
	u8 mst_pipe_mask;

	*pipe_mask = 0;
	*is_dp_mst = false;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return;

	tmp = intel_de_read(dev_priv, DDI_BUF_CTL(port));
	if (!(tmp & DDI_BUF_CTL_ENABLE))
		goto out;

	if (HAS_TRANSCODER_EDP(dev_priv) && port == PORT_A) {
		tmp = intel_de_read(dev_priv,
				    TRANS_DDI_FUNC_CTL(TRANSCODER_EDP));

		switch (tmp & TRANS_DDI_EDP_INPUT_MASK) {
		default:
			MISSING_CASE(tmp & TRANS_DDI_EDP_INPUT_MASK);
			/* fallthrough */
		case TRANS_DDI_EDP_INPUT_A_ON:
		case TRANS_DDI_EDP_INPUT_A_ONOFF:
			*pipe_mask = BIT(PIPE_A);
			break;
		case TRANS_DDI_EDP_INPUT_B_ONOFF:
			*pipe_mask = BIT(PIPE_B);
			break;
		case TRANS_DDI_EDP_INPUT_C_ONOFF:
			*pipe_mask = BIT(PIPE_C);
			break;
		}

		goto out;
	}

	mst_pipe_mask = 0;
	for_each_pipe(dev_priv, p) {
		enum transcoder cpu_transcoder = (enum transcoder)p;
		unsigned int port_mask, ddi_select;
		intel_wakeref_t trans_wakeref;

		trans_wakeref = intel_display_power_get_if_enabled(dev_priv,
								   POWER_DOMAIN_TRANSCODER(cpu_transcoder));
		if (!trans_wakeref)
			continue;

		if (INTEL_GEN(dev_priv) >= 12) {
			port_mask = TGL_TRANS_DDI_PORT_MASK;
			ddi_select = TGL_TRANS_DDI_SELECT_PORT(port);
		} else {
			port_mask = TRANS_DDI_PORT_MASK;
			ddi_select = TRANS_DDI_SELECT_PORT(port);
		}

		tmp = intel_de_read(dev_priv,
				    TRANS_DDI_FUNC_CTL(cpu_transcoder));
		intel_display_power_put(dev_priv, POWER_DOMAIN_TRANSCODER(cpu_transcoder),
					trans_wakeref);

		if ((tmp & port_mask) != ddi_select)
			continue;

		if ((tmp & TRANS_DDI_MODE_SELECT_MASK) ==
		    TRANS_DDI_MODE_SELECT_DP_MST)
			mst_pipe_mask |= BIT(p);

		*pipe_mask |= BIT(p);
	}

	if (!*pipe_mask)
		DRM_DEBUG_KMS("No pipe for [ENCODER:%d:%s] found\n",
			      encoder->base.base.id, encoder->base.name);

	if (!mst_pipe_mask && hweight8(*pipe_mask) > 1) {
		DRM_DEBUG_KMS("Multiple pipes for [ENCODER:%d:%s] (pipe_mask %02x)\n",
			      encoder->base.base.id, encoder->base.name,
			      *pipe_mask);
		*pipe_mask = BIT(ffs(*pipe_mask) - 1);
	}

	if (mst_pipe_mask && mst_pipe_mask != *pipe_mask)
		DRM_DEBUG_KMS("Conflicting MST and non-MST state for [ENCODER:%d:%s] (pipe_mask %02x mst_pipe_mask %02x)\n",
			      encoder->base.base.id, encoder->base.name,
			      *pipe_mask, mst_pipe_mask);
	else
		*is_dp_mst = mst_pipe_mask;

out:
	if (*pipe_mask && IS_GEN9_LP(dev_priv)) {
		tmp = intel_de_read(dev_priv, BXT_PHY_CTL(port));
		if ((tmp & (BXT_PHY_CMNLANE_POWERDOWN_ACK |
			    BXT_PHY_LANE_POWERDOWN_ACK |
			    BXT_PHY_LANE_ENABLED)) != BXT_PHY_LANE_ENABLED)
			DRM_ERROR("[ENCODER:%d:%s] enabled but PHY powered down? "
				  "(PHY_CTL %08x)\n", encoder->base.base.id,
				  encoder->base.name, tmp);
	}

	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);
}

bool intel_ddi_get_hw_state(struct intel_encoder *encoder,
			    enum pipe *pipe)
{
	u8 pipe_mask;
	bool is_mst;

	intel_ddi_get_encoder_pipes(encoder, &pipe_mask, &is_mst);

	if (is_mst || !pipe_mask)
		return false;

	*pipe = ffs(pipe_mask) - 1;

	return true;
}

static inline enum intel_display_power_domain
intel_ddi_main_link_aux_domain(struct intel_digital_port *dig_port)
{
	/* CNL+ HW requires corresponding AUX IOs to be powered up for PSR with
	 * DC states enabled at the same time, while for driver initiated AUX
	 * transfers we need the same AUX IOs to be powered but with DC states
	 * disabled. Accordingly use the AUX power domain here which leaves DC
	 * states enabled.
	 * However, for non-A AUX ports the corresponding non-EDP transcoders
	 * would have already enabled power well 2 and DC_OFF. This means we can
	 * acquire a wider POWER_DOMAIN_AUX_{B,C,D,F} reference instead of a
	 * specific AUX_IO reference without powering up any extra wells.
	 * Note that PSR is enabled only on Port A even though this function
	 * returns the correct domain for other ports too.
	 */
	return dig_port->aux_ch == AUX_CH_A ? POWER_DOMAIN_AUX_IO_A :
					      intel_aux_power_domain(dig_port);
}

static void intel_ddi_get_power_domains(struct intel_encoder *encoder,
					struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port;
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);

	/*
	 * TODO: Add support for MST encoders. Atm, the following should never
	 * happen since fake-MST encoders don't set their get_power_domains()
	 * hook.
	 */
	if (drm_WARN_ON(&dev_priv->drm,
			intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST)))
		return;

	dig_port = enc_to_dig_port(encoder);

	if (!intel_phy_is_tc(dev_priv, phy) ||
	    dig_port->tc_mode != TC_PORT_TBT_ALT)
		intel_display_power_get(dev_priv,
					dig_port->ddi_io_power_domain);

	/*
	 * AUX power is only needed for (e)DP mode, and for HDMI mode on TC
	 * ports.
	 */
	if (intel_crtc_has_dp_encoder(crtc_state) ||
	    intel_phy_is_tc(dev_priv, phy))
		intel_display_power_get(dev_priv,
					intel_ddi_main_link_aux_domain(dig_port));

	/*
	 * VDSC power is needed when DSC is enabled
	 */
	if (crtc_state->dsc.compression_enable)
		intel_display_power_get(dev_priv,
					intel_dsc_power_domain(crtc_state));
}

void intel_ddi_enable_pipe_clock(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_encoder *encoder = intel_ddi_get_crtc_encoder(crtc);
	enum port port = encoder->port;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (cpu_transcoder != TRANSCODER_EDP) {
		if (INTEL_GEN(dev_priv) >= 12)
			intel_de_write(dev_priv,
				       TRANS_CLK_SEL(cpu_transcoder),
				       TGL_TRANS_CLK_SEL_PORT(port));
		else
			intel_de_write(dev_priv,
				       TRANS_CLK_SEL(cpu_transcoder),
				       TRANS_CLK_SEL_PORT(port));
	}
}

void intel_ddi_disable_pipe_clock(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (cpu_transcoder != TRANSCODER_EDP) {
		if (INTEL_GEN(dev_priv) >= 12)
			intel_de_write(dev_priv,
				       TRANS_CLK_SEL(cpu_transcoder),
				       TGL_TRANS_CLK_SEL_DISABLED);
		else
			intel_de_write(dev_priv,
				       TRANS_CLK_SEL(cpu_transcoder),
				       TRANS_CLK_SEL_DISABLED);
	}
}

static void _skl_ddi_set_iboost(struct drm_i915_private *dev_priv,
				enum port port, u8 iboost)
{
	u32 tmp;

	tmp = intel_de_read(dev_priv, DISPIO_CR_TX_BMU_CR0);
	tmp &= ~(BALANCE_LEG_MASK(port) | BALANCE_LEG_DISABLE(port));
	if (iboost)
		tmp |= iboost << BALANCE_LEG_SHIFT(port);
	else
		tmp |= BALANCE_LEG_DISABLE(port);
	intel_de_write(dev_priv, DISPIO_CR_TX_BMU_CR0, tmp);
}

static void skl_ddi_set_iboost(struct intel_encoder *encoder,
			       int level, enum intel_output_type type)
{
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	u8 iboost;

	if (type == INTEL_OUTPUT_HDMI)
		iboost = intel_bios_hdmi_boost_level(encoder);
	else
		iboost = intel_bios_dp_boost_level(encoder);

	if (iboost == 0) {
		const struct ddi_buf_trans *ddi_translations;
		int n_entries;

		if (type == INTEL_OUTPUT_HDMI)
			ddi_translations = intel_ddi_get_buf_trans_hdmi(dev_priv, &n_entries);
		else if (type == INTEL_OUTPUT_EDP)
			ddi_translations = intel_ddi_get_buf_trans_edp(dev_priv, port, &n_entries);
		else
			ddi_translations = intel_ddi_get_buf_trans_dp(dev_priv, port, &n_entries);

		if (drm_WARN_ON_ONCE(&dev_priv->drm, !ddi_translations))
			return;
		if (drm_WARN_ON_ONCE(&dev_priv->drm, level >= n_entries))
			level = n_entries - 1;

		iboost = ddi_translations[level].i_boost;
	}

	/* Make sure that the requested I_boost is valid */
	if (iboost && iboost != 0x1 && iboost != 0x3 && iboost != 0x7) {
		DRM_ERROR("Invalid I_boost value %u\n", iboost);
		return;
	}

	_skl_ddi_set_iboost(dev_priv, port, iboost);

	if (port == PORT_A && intel_dig_port->max_lanes == 4)
		_skl_ddi_set_iboost(dev_priv, PORT_E, iboost);
}

static void bxt_ddi_vswing_sequence(struct intel_encoder *encoder,
				    int level, enum intel_output_type type)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	const struct bxt_ddi_buf_trans *ddi_translations;
	enum port port = encoder->port;
	int n_entries;

	if (type == INTEL_OUTPUT_HDMI)
		ddi_translations = bxt_get_buf_trans_hdmi(dev_priv, &n_entries);
	else if (type == INTEL_OUTPUT_EDP)
		ddi_translations = bxt_get_buf_trans_edp(dev_priv, &n_entries);
	else
		ddi_translations = bxt_get_buf_trans_dp(dev_priv, &n_entries);

	if (drm_WARN_ON_ONCE(&dev_priv->drm, !ddi_translations))
		return;
	if (drm_WARN_ON_ONCE(&dev_priv->drm, level >= n_entries))
		level = n_entries - 1;

	bxt_ddi_phy_set_signal_level(dev_priv, port,
				     ddi_translations[level].margin,
				     ddi_translations[level].scale,
				     ddi_translations[level].enable,
				     ddi_translations[level].deemphasis);
}

u8 intel_ddi_dp_voltage_max(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	enum port port = encoder->port;
	enum phy phy = intel_port_to_phy(dev_priv, port);
	int n_entries;

	if (INTEL_GEN(dev_priv) >= 12) {
		if (intel_phy_is_combo(dev_priv, phy))
			tgl_get_combo_buf_trans(dev_priv, encoder->type,
						intel_dp->link_rate, &n_entries);
		else
			n_entries = ARRAY_SIZE(tgl_dkl_phy_dp_ddi_trans);
	} else if (INTEL_GEN(dev_priv) == 11) {
		if (IS_ELKHARTLAKE(dev_priv))
			ehl_get_combo_buf_trans(dev_priv, encoder->type,
						intel_dp->link_rate, &n_entries);
		else if (intel_phy_is_combo(dev_priv, phy))
			icl_get_combo_buf_trans(dev_priv, encoder->type,
						intel_dp->link_rate, &n_entries);
		else
			n_entries = ARRAY_SIZE(icl_mg_phy_ddi_translations);
	} else if (IS_CANNONLAKE(dev_priv)) {
		if (encoder->type == INTEL_OUTPUT_EDP)
			cnl_get_buf_trans_edp(dev_priv, &n_entries);
		else
			cnl_get_buf_trans_dp(dev_priv, &n_entries);
	} else if (IS_GEN9_LP(dev_priv)) {
		if (encoder->type == INTEL_OUTPUT_EDP)
			bxt_get_buf_trans_edp(dev_priv, &n_entries);
		else
			bxt_get_buf_trans_dp(dev_priv, &n_entries);
	} else {
		if (encoder->type == INTEL_OUTPUT_EDP)
			intel_ddi_get_buf_trans_edp(dev_priv, port, &n_entries);
		else
			intel_ddi_get_buf_trans_dp(dev_priv, port, &n_entries);
	}

	if (drm_WARN_ON(&dev_priv->drm, n_entries < 1))
		n_entries = 1;
	if (drm_WARN_ON(&dev_priv->drm,
			n_entries > ARRAY_SIZE(index_to_dp_signal_levels)))
		n_entries = ARRAY_SIZE(index_to_dp_signal_levels);

	return index_to_dp_signal_levels[n_entries - 1] &
		DP_TRAIN_VOLTAGE_SWING_MASK;
}

/*
 * We assume that the full set of pre-emphasis values can be
 * used on all DDI platforms. Should that change we need to
 * rethink this code.
 */
u8 intel_ddi_dp_pre_emphasis_max(struct intel_encoder *encoder, u8 voltage_swing)
{
	switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
		return DP_TRAIN_PRE_EMPH_LEVEL_3;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
		return DP_TRAIN_PRE_EMPH_LEVEL_2;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
		return DP_TRAIN_PRE_EMPH_LEVEL_1;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_3:
	default:
		return DP_TRAIN_PRE_EMPH_LEVEL_0;
	}
}

static void cnl_ddi_vswing_program(struct intel_encoder *encoder,
				   int level, enum intel_output_type type)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	const struct cnl_ddi_buf_trans *ddi_translations;
	enum port port = encoder->port;
	int n_entries, ln;
	u32 val;

	if (type == INTEL_OUTPUT_HDMI)
		ddi_translations = cnl_get_buf_trans_hdmi(dev_priv, &n_entries);
	else if (type == INTEL_OUTPUT_EDP)
		ddi_translations = cnl_get_buf_trans_edp(dev_priv, &n_entries);
	else
		ddi_translations = cnl_get_buf_trans_dp(dev_priv, &n_entries);

	if (drm_WARN_ON_ONCE(&dev_priv->drm, !ddi_translations))
		return;
	if (drm_WARN_ON_ONCE(&dev_priv->drm, level >= n_entries))
		level = n_entries - 1;

	/* Set PORT_TX_DW5 Scaling Mode Sel to 010b. */
	val = intel_de_read(dev_priv, CNL_PORT_TX_DW5_LN0(port));
	val &= ~SCALING_MODE_SEL_MASK;
	val |= SCALING_MODE_SEL(2);
	intel_de_write(dev_priv, CNL_PORT_TX_DW5_GRP(port), val);

	/* Program PORT_TX_DW2 */
	val = intel_de_read(dev_priv, CNL_PORT_TX_DW2_LN0(port));
	val &= ~(SWING_SEL_LOWER_MASK | SWING_SEL_UPPER_MASK |
		 RCOMP_SCALAR_MASK);
	val |= SWING_SEL_UPPER(ddi_translations[level].dw2_swing_sel);
	val |= SWING_SEL_LOWER(ddi_translations[level].dw2_swing_sel);
	/* Rcomp scalar is fixed as 0x98 for every table entry */
	val |= RCOMP_SCALAR(0x98);
	intel_de_write(dev_priv, CNL_PORT_TX_DW2_GRP(port), val);

	/* Program PORT_TX_DW4 */
	/* We cannot write to GRP. It would overrite individual loadgen */
	for (ln = 0; ln < 4; ln++) {
		val = intel_de_read(dev_priv, CNL_PORT_TX_DW4_LN(ln, port));
		val &= ~(POST_CURSOR_1_MASK | POST_CURSOR_2_MASK |
			 CURSOR_COEFF_MASK);
		val |= POST_CURSOR_1(ddi_translations[level].dw4_post_cursor_1);
		val |= POST_CURSOR_2(ddi_translations[level].dw4_post_cursor_2);
		val |= CURSOR_COEFF(ddi_translations[level].dw4_cursor_coeff);
		intel_de_write(dev_priv, CNL_PORT_TX_DW4_LN(ln, port), val);
	}

	/* Program PORT_TX_DW5 */
	/* All DW5 values are fixed for every table entry */
	val = intel_de_read(dev_priv, CNL_PORT_TX_DW5_LN0(port));
	val &= ~RTERM_SELECT_MASK;
	val |= RTERM_SELECT(6);
	val |= TAP3_DISABLE;
	intel_de_write(dev_priv, CNL_PORT_TX_DW5_GRP(port), val);

	/* Program PORT_TX_DW7 */
	val = intel_de_read(dev_priv, CNL_PORT_TX_DW7_LN0(port));
	val &= ~N_SCALAR_MASK;
	val |= N_SCALAR(ddi_translations[level].dw7_n_scalar);
	intel_de_write(dev_priv, CNL_PORT_TX_DW7_GRP(port), val);
}

static void cnl_ddi_vswing_sequence(struct intel_encoder *encoder,
				    int level, enum intel_output_type type)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	int width, rate, ln;
	u32 val;

	if (type == INTEL_OUTPUT_HDMI) {
		width = 4;
		rate = 0; /* Rate is always < than 6GHz for HDMI */
	} else {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		width = intel_dp->lane_count;
		rate = intel_dp->link_rate;
	}

	/*
	 * 1. If port type is eDP or DP,
	 * set PORT_PCS_DW1 cmnkeeper_enable to 1b,
	 * else clear to 0b.
	 */
	val = intel_de_read(dev_priv, CNL_PORT_PCS_DW1_LN0(port));
	if (type != INTEL_OUTPUT_HDMI)
		val |= COMMON_KEEPER_EN;
	else
		val &= ~COMMON_KEEPER_EN;
	intel_de_write(dev_priv, CNL_PORT_PCS_DW1_GRP(port), val);

	/* 2. Program loadgen select */
	/*
	 * Program PORT_TX_DW4_LN depending on Bit rate and used lanes
	 * <= 6 GHz and 4 lanes (LN0=0, LN1=1, LN2=1, LN3=1)
	 * <= 6 GHz and 1,2 lanes (LN0=0, LN1=1, LN2=1, LN3=0)
	 * > 6 GHz (LN0=0, LN1=0, LN2=0, LN3=0)
	 */
	for (ln = 0; ln <= 3; ln++) {
		val = intel_de_read(dev_priv, CNL_PORT_TX_DW4_LN(ln, port));
		val &= ~LOADGEN_SELECT;

		if ((rate <= 600000 && width == 4 && ln >= 1)  ||
		    (rate <= 600000 && width < 4 && (ln == 1 || ln == 2))) {
			val |= LOADGEN_SELECT;
		}
		intel_de_write(dev_priv, CNL_PORT_TX_DW4_LN(ln, port), val);
	}

	/* 3. Set PORT_CL_DW5 SUS Clock Config to 11b */
	val = intel_de_read(dev_priv, CNL_PORT_CL1CM_DW5);
	val |= SUS_CLOCK_CONFIG;
	intel_de_write(dev_priv, CNL_PORT_CL1CM_DW5, val);

	/* 4. Clear training enable to change swing values */
	val = intel_de_read(dev_priv, CNL_PORT_TX_DW5_LN0(port));
	val &= ~TX_TRAINING_EN;
	intel_de_write(dev_priv, CNL_PORT_TX_DW5_GRP(port), val);

	/* 5. Program swing and de-emphasis */
	cnl_ddi_vswing_program(encoder, level, type);

	/* 6. Set training enable to trigger update */
	val = intel_de_read(dev_priv, CNL_PORT_TX_DW5_LN0(port));
	val |= TX_TRAINING_EN;
	intel_de_write(dev_priv, CNL_PORT_TX_DW5_GRP(port), val);
}

static void icl_ddi_combo_vswing_program(struct drm_i915_private *dev_priv,
					u32 level, enum phy phy, int type,
					int rate)
{
	const struct cnl_ddi_buf_trans *ddi_translations = NULL;
	u32 n_entries, val;
	int ln;

	if (INTEL_GEN(dev_priv) >= 12)
		ddi_translations = tgl_get_combo_buf_trans(dev_priv, type, rate,
							   &n_entries);
	else if (IS_ELKHARTLAKE(dev_priv))
		ddi_translations = ehl_get_combo_buf_trans(dev_priv, type, rate,
							   &n_entries);
	else
		ddi_translations = icl_get_combo_buf_trans(dev_priv, type, rate,
							   &n_entries);
	if (!ddi_translations)
		return;

	if (level >= n_entries) {
		DRM_DEBUG_KMS("DDI translation not found for level %d. Using %d instead.", level, n_entries - 1);
		level = n_entries - 1;
	}

	/* Set PORT_TX_DW5 */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW5_LN0(phy));
	val &= ~(SCALING_MODE_SEL_MASK | RTERM_SELECT_MASK |
		  TAP2_DISABLE | TAP3_DISABLE);
	val |= SCALING_MODE_SEL(0x2);
	val |= RTERM_SELECT(0x6);
	val |= TAP3_DISABLE;
	intel_de_write(dev_priv, ICL_PORT_TX_DW5_GRP(phy), val);

	/* Program PORT_TX_DW2 */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW2_LN0(phy));
	val &= ~(SWING_SEL_LOWER_MASK | SWING_SEL_UPPER_MASK |
		 RCOMP_SCALAR_MASK);
	val |= SWING_SEL_UPPER(ddi_translations[level].dw2_swing_sel);
	val |= SWING_SEL_LOWER(ddi_translations[level].dw2_swing_sel);
	/* Program Rcomp scalar for every table entry */
	val |= RCOMP_SCALAR(0x98);
	intel_de_write(dev_priv, ICL_PORT_TX_DW2_GRP(phy), val);

	/* Program PORT_TX_DW4 */
	/* We cannot write to GRP. It would overwrite individual loadgen. */
	for (ln = 0; ln <= 3; ln++) {
		val = intel_de_read(dev_priv, ICL_PORT_TX_DW4_LN(ln, phy));
		val &= ~(POST_CURSOR_1_MASK | POST_CURSOR_2_MASK |
			 CURSOR_COEFF_MASK);
		val |= POST_CURSOR_1(ddi_translations[level].dw4_post_cursor_1);
		val |= POST_CURSOR_2(ddi_translations[level].dw4_post_cursor_2);
		val |= CURSOR_COEFF(ddi_translations[level].dw4_cursor_coeff);
		intel_de_write(dev_priv, ICL_PORT_TX_DW4_LN(ln, phy), val);
	}

	/* Program PORT_TX_DW7 */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW7_LN0(phy));
	val &= ~N_SCALAR_MASK;
	val |= N_SCALAR(ddi_translations[level].dw7_n_scalar);
	intel_de_write(dev_priv, ICL_PORT_TX_DW7_GRP(phy), val);
}

static void icl_combo_phy_ddi_vswing_sequence(struct intel_encoder *encoder,
					      u32 level,
					      enum intel_output_type type)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	int width = 0;
	int rate = 0;
	u32 val;
	int ln = 0;

	if (type == INTEL_OUTPUT_HDMI) {
		width = 4;
		/* Rate is always < than 6GHz for HDMI */
	} else {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		width = intel_dp->lane_count;
		rate = intel_dp->link_rate;
	}

	/*
	 * 1. If port type is eDP or DP,
	 * set PORT_PCS_DW1 cmnkeeper_enable to 1b,
	 * else clear to 0b.
	 */
	val = intel_de_read(dev_priv, ICL_PORT_PCS_DW1_LN0(phy));
	if (type == INTEL_OUTPUT_HDMI)
		val &= ~COMMON_KEEPER_EN;
	else
		val |= COMMON_KEEPER_EN;
	intel_de_write(dev_priv, ICL_PORT_PCS_DW1_GRP(phy), val);

	/* 2. Program loadgen select */
	/*
	 * Program PORT_TX_DW4_LN depending on Bit rate and used lanes
	 * <= 6 GHz and 4 lanes (LN0=0, LN1=1, LN2=1, LN3=1)
	 * <= 6 GHz and 1,2 lanes (LN0=0, LN1=1, LN2=1, LN3=0)
	 * > 6 GHz (LN0=0, LN1=0, LN2=0, LN3=0)
	 */
	for (ln = 0; ln <= 3; ln++) {
		val = intel_de_read(dev_priv, ICL_PORT_TX_DW4_LN(ln, phy));
		val &= ~LOADGEN_SELECT;

		if ((rate <= 600000 && width == 4 && ln >= 1) ||
		    (rate <= 600000 && width < 4 && (ln == 1 || ln == 2))) {
			val |= LOADGEN_SELECT;
		}
		intel_de_write(dev_priv, ICL_PORT_TX_DW4_LN(ln, phy), val);
	}

	/* 3. Set PORT_CL_DW5 SUS Clock Config to 11b */
	val = intel_de_read(dev_priv, ICL_PORT_CL_DW5(phy));
	val |= SUS_CLOCK_CONFIG;
	intel_de_write(dev_priv, ICL_PORT_CL_DW5(phy), val);

	/* 4. Clear training enable to change swing values */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW5_LN0(phy));
	val &= ~TX_TRAINING_EN;
	intel_de_write(dev_priv, ICL_PORT_TX_DW5_GRP(phy), val);

	/* 5. Program swing and de-emphasis */
	icl_ddi_combo_vswing_program(dev_priv, level, phy, type, rate);

	/* 6. Set training enable to trigger update */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW5_LN0(phy));
	val |= TX_TRAINING_EN;
	intel_de_write(dev_priv, ICL_PORT_TX_DW5_GRP(phy), val);
}

static void icl_mg_phy_ddi_vswing_sequence(struct intel_encoder *encoder,
					   int link_clock,
					   u32 level)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, encoder->port);
	const struct icl_mg_phy_ddi_buf_trans *ddi_translations;
	u32 n_entries, val;
	int ln;

	n_entries = ARRAY_SIZE(icl_mg_phy_ddi_translations);
	ddi_translations = icl_mg_phy_ddi_translations;
	/* The table does not have values for level 3 and level 9. */
	if (level >= n_entries || level == 3 || level == 9) {
		DRM_DEBUG_KMS("DDI translation not found for level %d. Using %d instead.",
			      level, n_entries - 2);
		level = n_entries - 2;
	}

	/* Set MG_TX_LINK_PARAMS cri_use_fs32 to 0. */
	for (ln = 0; ln < 2; ln++) {
		val = intel_de_read(dev_priv, MG_TX1_LINK_PARAMS(ln, tc_port));
		val &= ~CRI_USE_FS32;
		intel_de_write(dev_priv, MG_TX1_LINK_PARAMS(ln, tc_port), val);

		val = intel_de_read(dev_priv, MG_TX2_LINK_PARAMS(ln, tc_port));
		val &= ~CRI_USE_FS32;
		intel_de_write(dev_priv, MG_TX2_LINK_PARAMS(ln, tc_port), val);
	}

	/* Program MG_TX_SWINGCTRL with values from vswing table */
	for (ln = 0; ln < 2; ln++) {
		val = intel_de_read(dev_priv, MG_TX1_SWINGCTRL(ln, tc_port));
		val &= ~CRI_TXDEEMPH_OVERRIDE_17_12_MASK;
		val |= CRI_TXDEEMPH_OVERRIDE_17_12(
			ddi_translations[level].cri_txdeemph_override_17_12);
		intel_de_write(dev_priv, MG_TX1_SWINGCTRL(ln, tc_port), val);

		val = intel_de_read(dev_priv, MG_TX2_SWINGCTRL(ln, tc_port));
		val &= ~CRI_TXDEEMPH_OVERRIDE_17_12_MASK;
		val |= CRI_TXDEEMPH_OVERRIDE_17_12(
			ddi_translations[level].cri_txdeemph_override_17_12);
		intel_de_write(dev_priv, MG_TX2_SWINGCTRL(ln, tc_port), val);
	}

	/* Program MG_TX_DRVCTRL with values from vswing table */
	for (ln = 0; ln < 2; ln++) {
		val = intel_de_read(dev_priv, MG_TX1_DRVCTRL(ln, tc_port));
		val &= ~(CRI_TXDEEMPH_OVERRIDE_11_6_MASK |
			 CRI_TXDEEMPH_OVERRIDE_5_0_MASK);
		val |= CRI_TXDEEMPH_OVERRIDE_5_0(
			ddi_translations[level].cri_txdeemph_override_5_0) |
			CRI_TXDEEMPH_OVERRIDE_11_6(
				ddi_translations[level].cri_txdeemph_override_11_6) |
			CRI_TXDEEMPH_OVERRIDE_EN;
		intel_de_write(dev_priv, MG_TX1_DRVCTRL(ln, tc_port), val);

		val = intel_de_read(dev_priv, MG_TX2_DRVCTRL(ln, tc_port));
		val &= ~(CRI_TXDEEMPH_OVERRIDE_11_6_MASK |
			 CRI_TXDEEMPH_OVERRIDE_5_0_MASK);
		val |= CRI_TXDEEMPH_OVERRIDE_5_0(
			ddi_translations[level].cri_txdeemph_override_5_0) |
			CRI_TXDEEMPH_OVERRIDE_11_6(
				ddi_translations[level].cri_txdeemph_override_11_6) |
			CRI_TXDEEMPH_OVERRIDE_EN;
		intel_de_write(dev_priv, MG_TX2_DRVCTRL(ln, tc_port), val);

		/* FIXME: Program CRI_LOADGEN_SEL after the spec is updated */
	}

	/*
	 * Program MG_CLKHUB<LN, port being used> with value from frequency table
	 * In case of Legacy mode on MG PHY, both TX1 and TX2 enabled so use the
	 * values from table for which TX1 and TX2 enabled.
	 */
	for (ln = 0; ln < 2; ln++) {
		val = intel_de_read(dev_priv, MG_CLKHUB(ln, tc_port));
		if (link_clock < 300000)
			val |= CFG_LOW_RATE_LKREN_EN;
		else
			val &= ~CFG_LOW_RATE_LKREN_EN;
		intel_de_write(dev_priv, MG_CLKHUB(ln, tc_port), val);
	}

	/* Program the MG_TX_DCC<LN, port being used> based on the link frequency */
	for (ln = 0; ln < 2; ln++) {
		val = intel_de_read(dev_priv, MG_TX1_DCC(ln, tc_port));
		val &= ~CFG_AMI_CK_DIV_OVERRIDE_VAL_MASK;
		if (link_clock <= 500000) {
			val &= ~CFG_AMI_CK_DIV_OVERRIDE_EN;
		} else {
			val |= CFG_AMI_CK_DIV_OVERRIDE_EN |
				CFG_AMI_CK_DIV_OVERRIDE_VAL(1);
		}
		intel_de_write(dev_priv, MG_TX1_DCC(ln, tc_port), val);

		val = intel_de_read(dev_priv, MG_TX2_DCC(ln, tc_port));
		val &= ~CFG_AMI_CK_DIV_OVERRIDE_VAL_MASK;
		if (link_clock <= 500000) {
			val &= ~CFG_AMI_CK_DIV_OVERRIDE_EN;
		} else {
			val |= CFG_AMI_CK_DIV_OVERRIDE_EN |
				CFG_AMI_CK_DIV_OVERRIDE_VAL(1);
		}
		intel_de_write(dev_priv, MG_TX2_DCC(ln, tc_port), val);
	}

	/* Program MG_TX_PISO_READLOAD with values from vswing table */
	for (ln = 0; ln < 2; ln++) {
		val = intel_de_read(dev_priv,
				    MG_TX1_PISO_READLOAD(ln, tc_port));
		val |= CRI_CALCINIT;
		intel_de_write(dev_priv, MG_TX1_PISO_READLOAD(ln, tc_port),
			       val);

		val = intel_de_read(dev_priv,
				    MG_TX2_PISO_READLOAD(ln, tc_port));
		val |= CRI_CALCINIT;
		intel_de_write(dev_priv, MG_TX2_PISO_READLOAD(ln, tc_port),
			       val);
	}
}

static void icl_ddi_vswing_sequence(struct intel_encoder *encoder,
				    int link_clock,
				    u32 level,
				    enum intel_output_type type)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);

	if (intel_phy_is_combo(dev_priv, phy))
		icl_combo_phy_ddi_vswing_sequence(encoder, level, type);
	else
		icl_mg_phy_ddi_vswing_sequence(encoder, link_clock, level);
}

static void
tgl_dkl_phy_ddi_vswing_sequence(struct intel_encoder *encoder, int link_clock,
				u32 level)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, encoder->port);
	const struct tgl_dkl_phy_ddi_buf_trans *ddi_translations;
	u32 n_entries, val, ln, dpcnt_mask, dpcnt_val;

	if (encoder->type == INTEL_OUTPUT_HDMI) {
		n_entries = ARRAY_SIZE(tgl_dkl_phy_hdmi_ddi_trans);
		ddi_translations = tgl_dkl_phy_hdmi_ddi_trans;
	} else {
		n_entries = ARRAY_SIZE(tgl_dkl_phy_dp_ddi_trans);
		ddi_translations = tgl_dkl_phy_dp_ddi_trans;
	}

	if (level >= n_entries)
		level = n_entries - 1;

	dpcnt_mask = (DKL_TX_PRESHOOT_COEFF_MASK |
		      DKL_TX_DE_EMPAHSIS_COEFF_MASK |
		      DKL_TX_VSWING_CONTROL_MASK);
	dpcnt_val = DKL_TX_VSWING_CONTROL(ddi_translations[level].dkl_vswing_control);
	dpcnt_val |= DKL_TX_DE_EMPHASIS_COEFF(ddi_translations[level].dkl_de_emphasis_control);
	dpcnt_val |= DKL_TX_PRESHOOT_COEFF(ddi_translations[level].dkl_preshoot_control);

	for (ln = 0; ln < 2; ln++) {
		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, ln));

		intel_de_write(dev_priv, DKL_TX_PMD_LANE_SUS(tc_port), 0);

		/* All the registers are RMW */
		val = intel_de_read(dev_priv, DKL_TX_DPCNTL0(tc_port));
		val &= ~dpcnt_mask;
		val |= dpcnt_val;
		intel_de_write(dev_priv, DKL_TX_DPCNTL0(tc_port), val);

		val = intel_de_read(dev_priv, DKL_TX_DPCNTL1(tc_port));
		val &= ~dpcnt_mask;
		val |= dpcnt_val;
		intel_de_write(dev_priv, DKL_TX_DPCNTL1(tc_port), val);

		val = intel_de_read(dev_priv, DKL_TX_DPCNTL2(tc_port));
		val &= ~DKL_TX_DP20BITMODE;
		intel_de_write(dev_priv, DKL_TX_DPCNTL2(tc_port), val);
	}
}

static void tgl_ddi_vswing_sequence(struct intel_encoder *encoder,
				    int link_clock,
				    u32 level,
				    enum intel_output_type type)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);

	if (intel_phy_is_combo(dev_priv, phy))
		icl_combo_phy_ddi_vswing_sequence(encoder, level, type);
	else
		tgl_dkl_phy_ddi_vswing_sequence(encoder, link_clock, level);
}

static u32 translate_signal_level(int signal_levels)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(index_to_dp_signal_levels); i++) {
		if (index_to_dp_signal_levels[i] == signal_levels)
			return i;
	}

	WARN(1, "Unsupported voltage swing/pre-emphasis level: 0x%x\n",
	     signal_levels);

	return 0;
}

static u32 intel_ddi_dp_level(struct intel_dp *intel_dp)
{
	u8 train_set = intel_dp->train_set[0];
	int signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
					 DP_TRAIN_PRE_EMPHASIS_MASK);

	return translate_signal_level(signal_levels);
}

u32 bxt_signal_levels(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dport->base.base.dev);
	struct intel_encoder *encoder = &dport->base;
	int level = intel_ddi_dp_level(intel_dp);

	if (INTEL_GEN(dev_priv) >= 12)
		tgl_ddi_vswing_sequence(encoder, intel_dp->link_rate,
					level, encoder->type);
	else if (INTEL_GEN(dev_priv) >= 11)
		icl_ddi_vswing_sequence(encoder, intel_dp->link_rate,
					level, encoder->type);
	else if (IS_CANNONLAKE(dev_priv))
		cnl_ddi_vswing_sequence(encoder, level, encoder->type);
	else
		bxt_ddi_vswing_sequence(encoder, level, encoder->type);

	return 0;
}

u32 ddi_signal_levels(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dport->base.base.dev);
	struct intel_encoder *encoder = &dport->base;
	int level = intel_ddi_dp_level(intel_dp);

	if (IS_GEN9_BC(dev_priv))
		skl_ddi_set_iboost(encoder, level, encoder->type);

	return DDI_BUF_TRANS_SELECT(level);
}

static inline
u32 icl_dpclka_cfgcr0_clk_off(struct drm_i915_private *dev_priv,
			      enum phy phy)
{
	if (intel_phy_is_combo(dev_priv, phy)) {
		return ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy);
	} else if (intel_phy_is_tc(dev_priv, phy)) {
		enum tc_port tc_port = intel_port_to_tc(dev_priv,
							(enum port)phy);

		return ICL_DPCLKA_CFGCR0_TC_CLK_OFF(tc_port);
	}

	return 0;
}

static void icl_map_plls_to_ports(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	u32 val;

	mutex_lock(&dev_priv->dpll.lock);

	val = intel_de_read(dev_priv, ICL_DPCLKA_CFGCR0);
	drm_WARN_ON(&dev_priv->drm,
		    (val & icl_dpclka_cfgcr0_clk_off(dev_priv, phy)) == 0);

	if (intel_phy_is_combo(dev_priv, phy)) {
		/*
		 * Even though this register references DDIs, note that we
		 * want to pass the PHY rather than the port (DDI).  For
		 * ICL, port=phy in all cases so it doesn't matter, but for
		 * EHL the bspec notes the following:
		 *
		 *   "DDID clock tied to DDIA clock, so DPCLKA_CFGCR0 DDIA
		 *   Clock Select chooses the PLL for both DDIA and DDID and
		 *   drives port A in all cases."
		 */
		val &= ~ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy);
		val |= ICL_DPCLKA_CFGCR0_DDI_CLK_SEL(pll->info->id, phy);
		intel_de_write(dev_priv, ICL_DPCLKA_CFGCR0, val);
		intel_de_posting_read(dev_priv, ICL_DPCLKA_CFGCR0);
	}

	val &= ~icl_dpclka_cfgcr0_clk_off(dev_priv, phy);
	intel_de_write(dev_priv, ICL_DPCLKA_CFGCR0, val);

	mutex_unlock(&dev_priv->dpll.lock);
}

static void icl_unmap_plls_to_ports(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	u32 val;

	mutex_lock(&dev_priv->dpll.lock);

	val = intel_de_read(dev_priv, ICL_DPCLKA_CFGCR0);
	val |= icl_dpclka_cfgcr0_clk_off(dev_priv, phy);
	intel_de_write(dev_priv, ICL_DPCLKA_CFGCR0, val);

	mutex_unlock(&dev_priv->dpll.lock);
}

static void icl_sanitize_port_clk_off(struct drm_i915_private *dev_priv,
				      u32 port_mask, bool ddi_clk_needed)
{
	enum port port;
	u32 val;

	val = intel_de_read(dev_priv, ICL_DPCLKA_CFGCR0);
	for_each_port_masked(port, port_mask) {
		enum phy phy = intel_port_to_phy(dev_priv, port);
		bool ddi_clk_off = val & icl_dpclka_cfgcr0_clk_off(dev_priv,
								   phy);

		if (ddi_clk_needed == !ddi_clk_off)
			continue;

		/*
		 * Punt on the case now where clock is gated, but it would
		 * be needed by the port. Something else is really broken then.
		 */
		if (drm_WARN_ON(&dev_priv->drm, ddi_clk_needed))
			continue;

		DRM_NOTE("PHY %c is disabled/in DSI mode with an ungated DDI clock, gate it\n",
			 phy_name(phy));
		val |= icl_dpclka_cfgcr0_clk_off(dev_priv, phy);
		intel_de_write(dev_priv, ICL_DPCLKA_CFGCR0, val);
	}
}

void icl_sanitize_encoder_pll_mapping(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 port_mask;
	bool ddi_clk_needed;

	/*
	 * In case of DP MST, we sanitize the primary encoder only, not the
	 * virtual ones.
	 */
	if (encoder->type == INTEL_OUTPUT_DP_MST)
		return;

	if (!encoder->base.crtc && intel_encoder_is_dp(encoder)) {
		u8 pipe_mask;
		bool is_mst;

		intel_ddi_get_encoder_pipes(encoder, &pipe_mask, &is_mst);
		/*
		 * In the unlikely case that BIOS enables DP in MST mode, just
		 * warn since our MST HW readout is incomplete.
		 */
		if (drm_WARN_ON(&dev_priv->drm, is_mst))
			return;
	}

	port_mask = BIT(encoder->port);
	ddi_clk_needed = encoder->base.crtc;

	if (encoder->type == INTEL_OUTPUT_DSI) {
		struct intel_encoder *other_encoder;

		port_mask = intel_dsi_encoder_ports(encoder);
		/*
		 * Sanity check that we haven't incorrectly registered another
		 * encoder using any of the ports of this DSI encoder.
		 */
		for_each_intel_encoder(&dev_priv->drm, other_encoder) {
			if (other_encoder == encoder)
				continue;

			if (drm_WARN_ON(&dev_priv->drm,
					port_mask & BIT(other_encoder->port)))
				return;
		}
		/*
		 * For DSI we keep the ddi clocks gated
		 * except during enable/disable sequence.
		 */
		ddi_clk_needed = false;
	}

	icl_sanitize_port_clk_off(dev_priv, port_mask, ddi_clk_needed);
}

static void intel_ddi_clk_select(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum phy phy = intel_port_to_phy(dev_priv, port);
	u32 val;
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;

	if (drm_WARN_ON(&dev_priv->drm, !pll))
		return;

	mutex_lock(&dev_priv->dpll.lock);

	if (INTEL_GEN(dev_priv) >= 11) {
		if (!intel_phy_is_combo(dev_priv, phy))
			intel_de_write(dev_priv, DDI_CLK_SEL(port),
				       icl_pll_to_ddi_clk_sel(encoder, crtc_state));
		else if (IS_ELKHARTLAKE(dev_priv) && port >= PORT_C)
			/*
			 * MG does not exist but the programming is required
			 * to ungate DDIC and DDID
			 */
			intel_de_write(dev_priv, DDI_CLK_SEL(port),
				       DDI_CLK_SEL_MG);
	} else if (IS_CANNONLAKE(dev_priv)) {
		/* Configure DPCLKA_CFGCR0 to map the DPLL to the DDI. */
		val = intel_de_read(dev_priv, DPCLKA_CFGCR0);
		val &= ~DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(port);
		val |= DPCLKA_CFGCR0_DDI_CLK_SEL(pll->info->id, port);
		intel_de_write(dev_priv, DPCLKA_CFGCR0, val);

		/*
		 * Configure DPCLKA_CFGCR0 to turn on the clock for the DDI.
		 * This step and the step before must be done with separate
		 * register writes.
		 */
		val = intel_de_read(dev_priv, DPCLKA_CFGCR0);
		val &= ~DPCLKA_CFGCR0_DDI_CLK_OFF(port);
		intel_de_write(dev_priv, DPCLKA_CFGCR0, val);
	} else if (IS_GEN9_BC(dev_priv)) {
		/* DDI -> PLL mapping  */
		val = intel_de_read(dev_priv, DPLL_CTRL2);

		val &= ~(DPLL_CTRL2_DDI_CLK_OFF(port) |
			 DPLL_CTRL2_DDI_CLK_SEL_MASK(port));
		val |= (DPLL_CTRL2_DDI_CLK_SEL(pll->info->id, port) |
			DPLL_CTRL2_DDI_SEL_OVERRIDE(port));

		intel_de_write(dev_priv, DPLL_CTRL2, val);

	} else if (INTEL_GEN(dev_priv) < 9) {
		intel_de_write(dev_priv, PORT_CLK_SEL(port),
			       hsw_pll_to_ddi_pll_sel(pll));
	}

	mutex_unlock(&dev_priv->dpll.lock);
}

static void intel_ddi_clk_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum phy phy = intel_port_to_phy(dev_priv, port);

	if (INTEL_GEN(dev_priv) >= 11) {
		if (!intel_phy_is_combo(dev_priv, phy) ||
		    (IS_ELKHARTLAKE(dev_priv) && port >= PORT_C))
			intel_de_write(dev_priv, DDI_CLK_SEL(port),
				       DDI_CLK_SEL_NONE);
	} else if (IS_CANNONLAKE(dev_priv)) {
		intel_de_write(dev_priv, DPCLKA_CFGCR0,
			       intel_de_read(dev_priv, DPCLKA_CFGCR0) | DPCLKA_CFGCR0_DDI_CLK_OFF(port));
	} else if (IS_GEN9_BC(dev_priv)) {
		intel_de_write(dev_priv, DPLL_CTRL2,
			       intel_de_read(dev_priv, DPLL_CTRL2) | DPLL_CTRL2_DDI_CLK_OFF(port));
	} else if (INTEL_GEN(dev_priv) < 9) {
		intel_de_write(dev_priv, PORT_CLK_SEL(port),
			       PORT_CLK_SEL_NONE);
	}
}

static void
icl_program_mg_dp_mode(struct intel_digital_port *intel_dig_port,
		       const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, intel_dig_port->base.port);
	u32 ln0, ln1, pin_assignment;
	u8 width;

	if (intel_dig_port->tc_mode == TC_PORT_TBT_ALT)
		return;

	if (INTEL_GEN(dev_priv) >= 12) {
		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, 0x0));
		ln0 = intel_de_read(dev_priv, DKL_DP_MODE(tc_port));
		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, 0x1));
		ln1 = intel_de_read(dev_priv, DKL_DP_MODE(tc_port));
	} else {
		ln0 = intel_de_read(dev_priv, MG_DP_MODE(0, tc_port));
		ln1 = intel_de_read(dev_priv, MG_DP_MODE(1, tc_port));
	}

	ln0 &= ~(MG_DP_MODE_CFG_DP_X1_MODE | MG_DP_MODE_CFG_DP_X1_MODE);
	ln1 &= ~(MG_DP_MODE_CFG_DP_X1_MODE | MG_DP_MODE_CFG_DP_X2_MODE);

	/* DPPATC */
	pin_assignment = intel_tc_port_get_pin_assignment_mask(intel_dig_port);
	width = crtc_state->lane_count;

	switch (pin_assignment) {
	case 0x0:
		drm_WARN_ON(&dev_priv->drm,
			    intel_dig_port->tc_mode != TC_PORT_LEGACY);
		if (width == 1) {
			ln1 |= MG_DP_MODE_CFG_DP_X1_MODE;
		} else {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}
		break;
	case 0x1:
		if (width == 4) {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}
		break;
	case 0x2:
		if (width == 2) {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}
		break;
	case 0x3:
	case 0x5:
		if (width == 1) {
			ln0 |= MG_DP_MODE_CFG_DP_X1_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X1_MODE;
		} else {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}
		break;
	case 0x4:
	case 0x6:
		if (width == 1) {
			ln0 |= MG_DP_MODE_CFG_DP_X1_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X1_MODE;
		} else {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}
		break;
	default:
		MISSING_CASE(pin_assignment);
	}

	if (INTEL_GEN(dev_priv) >= 12) {
		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, 0x0));
		intel_de_write(dev_priv, DKL_DP_MODE(tc_port), ln0);
		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, 0x1));
		intel_de_write(dev_priv, DKL_DP_MODE(tc_port), ln1);
	} else {
		intel_de_write(dev_priv, MG_DP_MODE(0, tc_port), ln0);
		intel_de_write(dev_priv, MG_DP_MODE(1, tc_port), ln1);
	}
}

static void intel_dp_sink_set_fec_ready(struct intel_dp *intel_dp,
					const struct intel_crtc_state *crtc_state)
{
	if (!crtc_state->fec_enable)
		return;

	if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_FEC_CONFIGURATION, DP_FEC_READY) <= 0)
		DRM_DEBUG_KMS("Failed to set FEC_READY in the sink\n");
}

static void intel_ddi_enable_fec(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp;
	u32 val;

	if (!crtc_state->fec_enable)
		return;

	intel_dp = enc_to_intel_dp(encoder);
	val = intel_de_read(dev_priv, intel_dp->regs.dp_tp_ctl);
	val |= DP_TP_CTL_FEC_ENABLE;
	intel_de_write(dev_priv, intel_dp->regs.dp_tp_ctl, val);

	if (intel_de_wait_for_set(dev_priv, intel_dp->regs.dp_tp_status,
				  DP_TP_STATUS_FEC_ENABLE_LIVE, 1))
		DRM_ERROR("Timed out waiting for FEC Enable Status\n");
}

static void intel_ddi_disable_fec_state(struct intel_encoder *encoder,
					const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp;
	u32 val;

	if (!crtc_state->fec_enable)
		return;

	intel_dp = enc_to_intel_dp(encoder);
	val = intel_de_read(dev_priv, intel_dp->regs.dp_tp_ctl);
	val &= ~DP_TP_CTL_FEC_ENABLE;
	intel_de_write(dev_priv, intel_dp->regs.dp_tp_ctl, val);
	intel_de_posting_read(dev_priv, intel_dp->regs.dp_tp_ctl);
}

static void tgl_ddi_pre_enable_dp(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool is_mst = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST);
	int level = intel_ddi_dp_level(intel_dp);
	enum transcoder transcoder = crtc_state->cpu_transcoder;

	intel_dp_set_link_params(intel_dp, crtc_state->port_clock,
				 crtc_state->lane_count, is_mst);

	intel_dp->regs.dp_tp_ctl = TGL_DP_TP_CTL(transcoder);
	intel_dp->regs.dp_tp_status = TGL_DP_TP_STATUS(transcoder);

	/*
	 * 1. Enable Power Wells
	 *
	 * This was handled at the beginning of intel_atomic_commit_tail(),
	 * before we called down into this function.
	 */

	/* 2. Enable Panel Power if PPS is required */
	intel_edp_panel_on(intel_dp);

	/*
	 * 3. For non-TBT Type-C ports, set FIA lane count
	 * (DFLEXDPSP.DPX4TXLATC)
	 *
	 * This was done before tgl_ddi_pre_enable_dp by
	 * hsw_crtc_enable()->intel_encoders_pre_pll_enable().
	 */

	/*
	 * 4. Enable the port PLL.
	 *
	 * The PLL enabling itself was already done before this function by
	 * hsw_crtc_enable()->intel_enable_shared_dpll().  We need only
	 * configure the PLL to port mapping here.
	 */
	intel_ddi_clk_select(encoder, crtc_state);

	/* 5. If IO power is controlled through PWR_WELL_CTL, Enable IO Power */
	if (!intel_phy_is_tc(dev_priv, phy) ||
	    dig_port->tc_mode != TC_PORT_TBT_ALT)
		intel_display_power_get(dev_priv,
					dig_port->ddi_io_power_domain);

	/* 6. Program DP_MODE */
	icl_program_mg_dp_mode(dig_port, crtc_state);

	/*
	 * 7. The rest of the below are substeps under the bspec's "Enable and
	 * Train Display Port" step.  Note that steps that are specific to
	 * MST will be handled by intel_mst_pre_enable_dp() before/after it
	 * calls into this function.  Also intel_mst_pre_enable_dp() only calls
	 * us when active_mst_links==0, so any steps designated for "single
	 * stream or multi-stream master transcoder" can just be performed
	 * unconditionally here.
	 */

	/*
	 * 7.a Configure Transcoder Clock Select to direct the Port clock to the
	 * Transcoder.
	 */
	intel_ddi_enable_pipe_clock(crtc_state);

	/*
	 * 7.b Configure TRANS_DDI_FUNC_CTL DDI Select, DDI Mode Select & MST
	 * Transport Select
	 */
	intel_ddi_config_transcoder_func(crtc_state);

	/*
	 * 7.c Configure & enable DP_TP_CTL with link training pattern 1
	 * selected
	 *
	 * This will be handled by the intel_dp_start_link_train() farther
	 * down this function.
	 */

	/* 7.e Configure voltage swing and related IO settings */
	tgl_ddi_vswing_sequence(encoder, crtc_state->port_clock, level,
				encoder->type);

	/*
	 * 7.f Combo PHY: Configure PORT_CL_DW10 Static Power Down to power up
	 * the used lanes of the DDI.
	 */
	if (intel_phy_is_combo(dev_priv, phy)) {
		bool lane_reversal =
			dig_port->saved_port_bits & DDI_BUF_PORT_REVERSAL;

		intel_combo_phy_power_up_lanes(dev_priv, phy, false,
					       crtc_state->lane_count,
					       lane_reversal);
	}

	/*
	 * 7.g Configure and enable DDI_BUF_CTL
	 * 7.h Wait for DDI_BUF_CTL DDI Idle Status = 0b (Not Idle), timeout
	 *     after 500 us.
	 *
	 * We only configure what the register value will be here.  Actual
	 * enabling happens during link training farther down.
	 */
	intel_ddi_init_dp_buf_reg(encoder);

	if (!is_mst)
		intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_ON);

	intel_dp_sink_set_decompression_state(intel_dp, crtc_state, true);
	/*
	 * DDI FEC: "anticipates enabling FEC encoding sets the FEC_READY bit
	 * in the FEC_CONFIGURATION register to 1 before initiating link
	 * training
	 */
	intel_dp_sink_set_fec_ready(intel_dp, crtc_state);

	/*
	 * 7.i Follow DisplayPort specification training sequence (see notes for
	 *     failure handling)
	 * 7.j If DisplayPort multi-stream - Set DP_TP_CTL link training to Idle
	 *     Pattern, wait for 5 idle patterns (DP_TP_STATUS Min_Idles_Sent)
	 *     (timeout after 800 us)
	 */
	intel_dp_start_link_train(intel_dp);

	/* 7.k Set DP_TP_CTL link training to Normal */
	if (!is_trans_port_sync_mode(crtc_state))
		intel_dp_stop_link_train(intel_dp);

	/* 7.l Configure and enable FEC if needed */
	intel_ddi_enable_fec(encoder, crtc_state);
	intel_dsc_enable(encoder, crtc_state);
}

static void hsw_ddi_pre_enable_dp(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum phy phy = intel_port_to_phy(dev_priv, port);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool is_mst = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST);
	int level = intel_ddi_dp_level(intel_dp);

	if (INTEL_GEN(dev_priv) < 11)
		drm_WARN_ON(&dev_priv->drm,
			    is_mst && (port == PORT_A || port == PORT_E));
	else
		drm_WARN_ON(&dev_priv->drm, is_mst && port == PORT_A);

	intel_dp_set_link_params(intel_dp, crtc_state->port_clock,
				 crtc_state->lane_count, is_mst);

	intel_edp_panel_on(intel_dp);

	intel_ddi_clk_select(encoder, crtc_state);

	if (!intel_phy_is_tc(dev_priv, phy) ||
	    dig_port->tc_mode != TC_PORT_TBT_ALT)
		intel_display_power_get(dev_priv,
					dig_port->ddi_io_power_domain);

	icl_program_mg_dp_mode(dig_port, crtc_state);

	if (INTEL_GEN(dev_priv) >= 11)
		icl_ddi_vswing_sequence(encoder, crtc_state->port_clock,
					level, encoder->type);
	else if (IS_CANNONLAKE(dev_priv))
		cnl_ddi_vswing_sequence(encoder, level, encoder->type);
	else if (IS_GEN9_LP(dev_priv))
		bxt_ddi_vswing_sequence(encoder, level, encoder->type);
	else
		intel_prepare_dp_ddi_buffers(encoder, crtc_state);

	if (intel_phy_is_combo(dev_priv, phy)) {
		bool lane_reversal =
			dig_port->saved_port_bits & DDI_BUF_PORT_REVERSAL;

		intel_combo_phy_power_up_lanes(dev_priv, phy, false,
					       crtc_state->lane_count,
					       lane_reversal);
	}

	intel_ddi_init_dp_buf_reg(encoder);
	if (!is_mst)
		intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_ON);
	intel_dp_sink_set_decompression_state(intel_dp, crtc_state,
					      true);
	intel_dp_sink_set_fec_ready(intel_dp, crtc_state);
	intel_dp_start_link_train(intel_dp);
	if ((port != PORT_A || INTEL_GEN(dev_priv) >= 9) &&
	    !is_trans_port_sync_mode(crtc_state))
		intel_dp_stop_link_train(intel_dp);

	intel_ddi_enable_fec(encoder, crtc_state);

	if (!is_mst)
		intel_ddi_enable_pipe_clock(crtc_state);

	intel_dsc_enable(encoder, crtc_state);
}

static void intel_ddi_pre_enable_dp(struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state,
				    const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (INTEL_GEN(dev_priv) >= 12)
		tgl_ddi_pre_enable_dp(encoder, crtc_state, conn_state);
	else
		hsw_ddi_pre_enable_dp(encoder, crtc_state, conn_state);

	/* MST will call a setting of MSA after an allocating of Virtual Channel
	 * from MST encoder pre_enable callback.
	 */
	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST)) {
		intel_ddi_set_dp_msa(crtc_state, conn_state);

		intel_dp_set_m_n(crtc_state, M1_N1);
	}
}

static void intel_ddi_pre_enable_hdmi(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state,
				      const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(encoder);
	struct intel_hdmi *intel_hdmi = &intel_dig_port->hdmi;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int level = intel_ddi_hdmi_level(encoder);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, true);
	intel_ddi_clk_select(encoder, crtc_state);

	intel_display_power_get(dev_priv, dig_port->ddi_io_power_domain);

	icl_program_mg_dp_mode(dig_port, crtc_state);

	if (INTEL_GEN(dev_priv) >= 12)
		tgl_ddi_vswing_sequence(encoder, crtc_state->port_clock,
					level, INTEL_OUTPUT_HDMI);
	else if (INTEL_GEN(dev_priv) == 11)
		icl_ddi_vswing_sequence(encoder, crtc_state->port_clock,
					level, INTEL_OUTPUT_HDMI);
	else if (IS_CANNONLAKE(dev_priv))
		cnl_ddi_vswing_sequence(encoder, level, INTEL_OUTPUT_HDMI);
	else if (IS_GEN9_LP(dev_priv))
		bxt_ddi_vswing_sequence(encoder, level, INTEL_OUTPUT_HDMI);
	else
		intel_prepare_hdmi_ddi_buffers(encoder, level);

	if (IS_GEN9_BC(dev_priv))
		skl_ddi_set_iboost(encoder, level, INTEL_OUTPUT_HDMI);

	intel_ddi_enable_pipe_clock(crtc_state);

	intel_dig_port->set_infoframes(encoder,
				       crtc_state->has_infoframe,
				       crtc_state, conn_state);
}

static void intel_ddi_pre_enable(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state,
				 const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	/*
	 * When called from DP MST code:
	 * - conn_state will be NULL
	 * - encoder will be the main encoder (ie. mst->primary)
	 * - the main connector associated with this port
	 *   won't be active or linked to a crtc
	 * - crtc_state will be the state of the first stream to
	 *   be activated on this port, and it may not be the same
	 *   stream that will be deactivated last, but each stream
	 *   should have a state that is identical when it comes to
	 *   the DP link parameteres
	 */

	drm_WARN_ON(&dev_priv->drm, crtc_state->has_pch_encoder);

	if (INTEL_GEN(dev_priv) >= 11)
		icl_map_plls_to_ports(encoder, crtc_state);

	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		intel_ddi_pre_enable_hdmi(encoder, crtc_state, conn_state);
	} else {
		struct intel_lspcon *lspcon =
				enc_to_intel_lspcon(encoder);

		intel_ddi_pre_enable_dp(encoder, crtc_state, conn_state);
		if (lspcon->active) {
			struct intel_digital_port *dig_port =
					enc_to_dig_port(encoder);

			dig_port->set_infoframes(encoder,
						 crtc_state->has_infoframe,
						 crtc_state, conn_state);
		}
	}
}

static void intel_disable_ddi_buf(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	bool wait = false;
	u32 val;

	val = intel_de_read(dev_priv, DDI_BUF_CTL(port));
	if (val & DDI_BUF_CTL_ENABLE) {
		val &= ~DDI_BUF_CTL_ENABLE;
		intel_de_write(dev_priv, DDI_BUF_CTL(port), val);
		wait = true;
	}

	if (intel_crtc_has_dp_encoder(crtc_state)) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		val = intel_de_read(dev_priv, intel_dp->regs.dp_tp_ctl);
		val &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
		val |= DP_TP_CTL_LINK_TRAIN_PAT1;
		intel_de_write(dev_priv, intel_dp->regs.dp_tp_ctl, val);
	}

	/* Disable FEC in DP Sink */
	intel_ddi_disable_fec_state(encoder, crtc_state);

	if (wait)
		intel_wait_ddi_buf_idle(dev_priv, port);
}

static void intel_ddi_post_disable_dp(struct intel_encoder *encoder,
				      const struct intel_crtc_state *old_crtc_state,
				      const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp = &dig_port->dp;
	bool is_mst = intel_crtc_has_type(old_crtc_state,
					  INTEL_OUTPUT_DP_MST);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);

	/*
	 * Power down sink before disabling the port, otherwise we end
	 * up getting interrupts from the sink on detecting link loss.
	 */
	intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_OFF);

	if (INTEL_GEN(dev_priv) >= 12) {
		if (is_mst) {
			enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;
			u32 val;

			val = intel_de_read(dev_priv,
					    TRANS_DDI_FUNC_CTL(cpu_transcoder));
			val &= ~(TGL_TRANS_DDI_PORT_MASK |
				 TRANS_DDI_MODE_SELECT_MASK);
			intel_de_write(dev_priv,
				       TRANS_DDI_FUNC_CTL(cpu_transcoder),
				       val);
		}
	} else {
		if (!is_mst)
			intel_ddi_disable_pipe_clock(old_crtc_state);
	}

	intel_disable_ddi_buf(encoder, old_crtc_state);

	/*
	 * From TGL spec: "If single stream or multi-stream master transcoder:
	 * Configure Transcoder Clock select to direct no clock to the
	 * transcoder"
	 */
	if (INTEL_GEN(dev_priv) >= 12)
		intel_ddi_disable_pipe_clock(old_crtc_state);

	intel_edp_panel_vdd_on(intel_dp);
	intel_edp_panel_off(intel_dp);

	if (!intel_phy_is_tc(dev_priv, phy) ||
	    dig_port->tc_mode != TC_PORT_TBT_ALT)
		intel_display_power_put_unchecked(dev_priv,
						  dig_port->ddi_io_power_domain);

	intel_ddi_clk_disable(encoder);
}

static void intel_ddi_post_disable_hdmi(struct intel_encoder *encoder,
					const struct intel_crtc_state *old_crtc_state,
					const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_hdmi *intel_hdmi = &dig_port->hdmi;

	dig_port->set_infoframes(encoder, false,
				 old_crtc_state, old_conn_state);

	intel_ddi_disable_pipe_clock(old_crtc_state);

	intel_disable_ddi_buf(encoder, old_crtc_state);

	intel_display_power_put_unchecked(dev_priv,
					  dig_port->ddi_io_power_domain);

	intel_ddi_clk_disable(encoder);

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, false);
}

static void icl_disable_transcoder_port_sync(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	if (old_crtc_state->master_transcoder == INVALID_TRANSCODER)
		return;

	DRM_DEBUG_KMS("Disabling Transcoder Port Sync on Slave Transcoder %s\n",
		      transcoder_name(old_crtc_state->cpu_transcoder));

	intel_de_write(dev_priv,
		       TRANS_DDI_FUNC_CTL2(old_crtc_state->cpu_transcoder), 0);
}

static void intel_ddi_post_disable(struct intel_encoder *encoder,
				   const struct intel_crtc_state *old_crtc_state,
				   const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	bool is_tc_port = intel_phy_is_tc(dev_priv, phy);

	if (!intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_DP_MST)) {
		intel_crtc_vblank_off(old_crtc_state);

		intel_disable_pipe(old_crtc_state);

		if (INTEL_GEN(dev_priv) >= 11)
			icl_disable_transcoder_port_sync(old_crtc_state);

		intel_ddi_disable_transcoder_func(old_crtc_state);

		intel_dsc_disable(old_crtc_state);

		if (INTEL_GEN(dev_priv) >= 9)
			skl_scaler_disable(old_crtc_state);
		else
			ilk_pfit_disable(old_crtc_state);
	}

	/*
	 * When called from DP MST code:
	 * - old_conn_state will be NULL
	 * - encoder will be the main encoder (ie. mst->primary)
	 * - the main connector associated with this port
	 *   won't be active or linked to a crtc
	 * - old_crtc_state will be the state of the last stream to
	 *   be deactivated on this port, and it may not be the same
	 *   stream that was activated last, but each stream
	 *   should have a state that is identical when it comes to
	 *   the DP link parameteres
	 */

	if (intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_HDMI))
		intel_ddi_post_disable_hdmi(encoder,
					    old_crtc_state, old_conn_state);
	else
		intel_ddi_post_disable_dp(encoder,
					  old_crtc_state, old_conn_state);

	if (INTEL_GEN(dev_priv) >= 11)
		icl_unmap_plls_to_ports(encoder);

	if (intel_crtc_has_dp_encoder(old_crtc_state) || is_tc_port)
		intel_display_power_put_unchecked(dev_priv,
						  intel_ddi_main_link_aux_domain(dig_port));

	if (is_tc_port)
		intel_tc_port_put_link(dig_port);
}

void intel_ddi_fdi_post_disable(struct intel_encoder *encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 val;

	/*
	 * Bspec lists this as both step 13 (before DDI_BUF_CTL disable)
	 * and step 18 (after clearing PORT_CLK_SEL). Based on a BUN,
	 * step 13 is the correct place for it. Step 18 is where it was
	 * originally before the BUN.
	 */
	val = intel_de_read(dev_priv, FDI_RX_CTL(PIPE_A));
	val &= ~FDI_RX_ENABLE;
	intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), val);

	intel_disable_ddi_buf(encoder, old_crtc_state);
	intel_ddi_clk_disable(encoder);

	val = intel_de_read(dev_priv, FDI_RX_MISC(PIPE_A));
	val &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
	val |= FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2);
	intel_de_write(dev_priv, FDI_RX_MISC(PIPE_A), val);

	val = intel_de_read(dev_priv, FDI_RX_CTL(PIPE_A));
	val &= ~FDI_PCDCLK;
	intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), val);

	val = intel_de_read(dev_priv, FDI_RX_CTL(PIPE_A));
	val &= ~FDI_RX_PLL_ENABLE;
	intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), val);
}

static void intel_enable_ddi_dp(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state,
				const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	enum port port = encoder->port;

	if (port == PORT_A && INTEL_GEN(dev_priv) < 9)
		intel_dp_stop_link_train(intel_dp);

	intel_edp_backlight_on(crtc_state, conn_state);
	intel_psr_enable(intel_dp, crtc_state);
	intel_dp_vsc_enable(intel_dp, crtc_state, conn_state);
	intel_dp_hdr_metadata_enable(intel_dp, crtc_state, conn_state);
	intel_edp_drrs_enable(intel_dp, crtc_state);

	if (crtc_state->has_audio)
		intel_audio_codec_enable(encoder, crtc_state, conn_state);
}

static i915_reg_t
gen9_chicken_trans_reg_by_port(struct drm_i915_private *dev_priv,
			       enum port port)
{
	static const enum transcoder trans[] = {
		[PORT_A] = TRANSCODER_EDP,
		[PORT_B] = TRANSCODER_A,
		[PORT_C] = TRANSCODER_B,
		[PORT_D] = TRANSCODER_C,
		[PORT_E] = TRANSCODER_A,
	};

	drm_WARN_ON(&dev_priv->drm, INTEL_GEN(dev_priv) < 9);

	if (drm_WARN_ON(&dev_priv->drm, port < PORT_A || port > PORT_E))
		port = PORT_A;

	return CHICKEN_TRANS(trans[port]);
}

static void intel_enable_ddi_hdmi(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_connector *connector = conn_state->connector;
	enum port port = encoder->port;

	if (!intel_hdmi_handle_sink_scrambling(encoder, connector,
					       crtc_state->hdmi_high_tmds_clock_ratio,
					       crtc_state->hdmi_scrambling))
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] Failed to configure sink "
			      "scrambling/TMDS bit clock ratio\n",
			       connector->base.id, connector->name);

	/* Display WA #1143: skl,kbl,cfl */
	if (IS_GEN9_BC(dev_priv)) {
		/*
		 * For some reason these chicken bits have been
		 * stuffed into a transcoder register, event though
		 * the bits affect a specific DDI port rather than
		 * a specific transcoder.
		 */
		i915_reg_t reg = gen9_chicken_trans_reg_by_port(dev_priv, port);
		u32 val;

		val = intel_de_read(dev_priv, reg);

		if (port == PORT_E)
			val |= DDIE_TRAINING_OVERRIDE_ENABLE |
				DDIE_TRAINING_OVERRIDE_VALUE;
		else
			val |= DDI_TRAINING_OVERRIDE_ENABLE |
				DDI_TRAINING_OVERRIDE_VALUE;

		intel_de_write(dev_priv, reg, val);
		intel_de_posting_read(dev_priv, reg);

		udelay(1);

		if (port == PORT_E)
			val &= ~(DDIE_TRAINING_OVERRIDE_ENABLE |
				 DDIE_TRAINING_OVERRIDE_VALUE);
		else
			val &= ~(DDI_TRAINING_OVERRIDE_ENABLE |
				 DDI_TRAINING_OVERRIDE_VALUE);

		intel_de_write(dev_priv, reg, val);
	}

	/* In HDMI/DVI mode, the port width, and swing/emphasis values
	 * are ignored so nothing special needs to be done besides
	 * enabling the port.
	 */
	intel_de_write(dev_priv, DDI_BUF_CTL(port),
		       dig_port->saved_port_bits | DDI_BUF_CTL_ENABLE);

	if (crtc_state->has_audio)
		intel_audio_codec_enable(encoder, crtc_state, conn_state);
}

static void intel_enable_ddi(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state)
{
	WARN_ON(crtc_state->has_pch_encoder);

	intel_enable_pipe(crtc_state);

	intel_crtc_vblank_on(crtc_state);

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		intel_enable_ddi_hdmi(encoder, crtc_state, conn_state);
	else
		intel_enable_ddi_dp(encoder, crtc_state, conn_state);

	/* Enable hdcp if it's desired */
	if (conn_state->content_protection ==
	    DRM_MODE_CONTENT_PROTECTION_DESIRED)
		intel_hdcp_enable(to_intel_connector(conn_state->connector),
				  crtc_state->cpu_transcoder,
				  (u8)conn_state->hdcp_content_type);
}

static void intel_disable_ddi_dp(struct intel_encoder *encoder,
				 const struct intel_crtc_state *old_crtc_state,
				 const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_dp->link_trained = false;

	if (old_crtc_state->has_audio)
		intel_audio_codec_disable(encoder,
					  old_crtc_state, old_conn_state);

	intel_edp_drrs_disable(intel_dp, old_crtc_state);
	intel_psr_disable(intel_dp, old_crtc_state);
	intel_edp_backlight_off(old_conn_state);
	/* Disable the decompression in DP Sink */
	intel_dp_sink_set_decompression_state(intel_dp, old_crtc_state,
					      false);
}

static void intel_disable_ddi_hdmi(struct intel_encoder *encoder,
				   const struct intel_crtc_state *old_crtc_state,
				   const struct drm_connector_state *old_conn_state)
{
	struct drm_connector *connector = old_conn_state->connector;

	if (old_crtc_state->has_audio)
		intel_audio_codec_disable(encoder,
					  old_crtc_state, old_conn_state);

	if (!intel_hdmi_handle_sink_scrambling(encoder, connector,
					       false, false))
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] Failed to reset sink scrambling/TMDS bit clock ratio\n",
			      connector->base.id, connector->name);
}

static void intel_disable_ddi(struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state)
{
	intel_hdcp_disable(to_intel_connector(old_conn_state->connector));

	if (intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_HDMI))
		intel_disable_ddi_hdmi(encoder, old_crtc_state, old_conn_state);
	else
		intel_disable_ddi_dp(encoder, old_crtc_state, old_conn_state);
}

static void intel_ddi_update_pipe_dp(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state,
				     const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_ddi_set_dp_msa(crtc_state, conn_state);

	intel_psr_update(intel_dp, crtc_state);
	intel_edp_drrs_enable(intel_dp, crtc_state);

	intel_panel_update_backlight(encoder, crtc_state, conn_state);
}

static void intel_ddi_update_pipe(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{

	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		intel_ddi_update_pipe_dp(encoder, crtc_state, conn_state);

	intel_hdcp_update_pipe(encoder, crtc_state, conn_state);
}

static void
intel_ddi_update_prepare(struct intel_atomic_state *state,
			 struct intel_encoder *encoder,
			 struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		crtc ? intel_atomic_get_new_crtc_state(state, crtc) : NULL;
	int required_lanes = crtc_state ? crtc_state->lane_count : 1;

	WARN_ON(crtc && crtc->active);

	intel_tc_port_get_link(enc_to_dig_port(encoder),
		               required_lanes);
	if (crtc_state && crtc_state->hw.active)
		intel_update_active_dpll(state, crtc, encoder);
}

static void
intel_ddi_update_complete(struct intel_atomic_state *state,
			  struct intel_encoder *encoder,
			  struct intel_crtc *crtc)
{
	intel_tc_port_put_link(enc_to_dig_port(encoder));
}

static void
intel_ddi_pre_pll_enable(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state,
			 const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	bool is_tc_port = intel_phy_is_tc(dev_priv, phy);

	if (is_tc_port)
		intel_tc_port_get_link(dig_port, crtc_state->lane_count);

	if (intel_crtc_has_dp_encoder(crtc_state) || is_tc_port)
		intel_display_power_get(dev_priv,
					intel_ddi_main_link_aux_domain(dig_port));

	if (is_tc_port && dig_port->tc_mode != TC_PORT_TBT_ALT)
		/*
		 * Program the lane count for static/dynamic connections on
		 * Type-C ports.  Skip this step for TBT.
		 */
		intel_tc_port_set_fia_lane_count(dig_port, crtc_state->lane_count);
	else if (IS_GEN9_LP(dev_priv))
		bxt_ddi_phy_set_lane_optim_mask(encoder,
						crtc_state->lane_lat_optim_mask);
}

static void intel_ddi_prepare_link_retrain(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv =
		to_i915(intel_dig_port->base.base.dev);
	enum port port = intel_dig_port->base.port;
	u32 dp_tp_ctl, ddi_buf_ctl;
	bool wait = false;

	dp_tp_ctl = intel_de_read(dev_priv, intel_dp->regs.dp_tp_ctl);

	if (dp_tp_ctl & DP_TP_CTL_ENABLE) {
		ddi_buf_ctl = intel_de_read(dev_priv, DDI_BUF_CTL(port));
		if (ddi_buf_ctl & DDI_BUF_CTL_ENABLE) {
			intel_de_write(dev_priv, DDI_BUF_CTL(port),
				       ddi_buf_ctl & ~DDI_BUF_CTL_ENABLE);
			wait = true;
		}

		dp_tp_ctl &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
		dp_tp_ctl |= DP_TP_CTL_LINK_TRAIN_PAT1;
		intel_de_write(dev_priv, intel_dp->regs.dp_tp_ctl, dp_tp_ctl);
		intel_de_posting_read(dev_priv, intel_dp->regs.dp_tp_ctl);

		if (wait)
			intel_wait_ddi_buf_idle(dev_priv, port);
	}

	dp_tp_ctl = DP_TP_CTL_ENABLE |
		    DP_TP_CTL_LINK_TRAIN_PAT1 | DP_TP_CTL_SCRAMBLE_DISABLE;
	if (intel_dp->link_mst)
		dp_tp_ctl |= DP_TP_CTL_MODE_MST;
	else {
		dp_tp_ctl |= DP_TP_CTL_MODE_SST;
		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			dp_tp_ctl |= DP_TP_CTL_ENHANCED_FRAME_ENABLE;
	}
	intel_de_write(dev_priv, intel_dp->regs.dp_tp_ctl, dp_tp_ctl);
	intel_de_posting_read(dev_priv, intel_dp->regs.dp_tp_ctl);

	intel_dp->DP |= DDI_BUF_CTL_ENABLE;
	intel_de_write(dev_priv, DDI_BUF_CTL(port), intel_dp->DP);
	intel_de_posting_read(dev_priv, DDI_BUF_CTL(port));

	udelay(600);
}

static bool intel_ddi_is_audio_enabled(struct drm_i915_private *dev_priv,
				       enum transcoder cpu_transcoder)
{
	if (cpu_transcoder == TRANSCODER_EDP)
		return false;

	if (!intel_display_power_is_enabled(dev_priv, POWER_DOMAIN_AUDIO))
		return false;

	return intel_de_read(dev_priv, HSW_AUD_PIN_ELD_CP_VLD) &
		AUDIO_OUTPUT_ENABLE(cpu_transcoder);
}

void intel_ddi_compute_min_voltage_level(struct drm_i915_private *dev_priv,
					 struct intel_crtc_state *crtc_state)
{
	if (INTEL_GEN(dev_priv) >= 12 && crtc_state->port_clock > 594000)
		crtc_state->min_voltage_level = 2;
	else if (IS_ELKHARTLAKE(dev_priv) && crtc_state->port_clock > 594000)
		crtc_state->min_voltage_level = 3;
	else if (INTEL_GEN(dev_priv) >= 11 && crtc_state->port_clock > 594000)
		crtc_state->min_voltage_level = 1;
	else if (IS_CANNONLAKE(dev_priv) && crtc_state->port_clock > 594000)
		crtc_state->min_voltage_level = 2;
}

void intel_ddi_get_config(struct intel_encoder *encoder,
			  struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(pipe_config->uapi.crtc);
	enum transcoder cpu_transcoder = pipe_config->cpu_transcoder;
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	u32 temp, flags = 0;

	/* XXX: DSI transcoder paranoia */
	if (drm_WARN_ON(&dev_priv->drm, transcoder_is_dsi(cpu_transcoder)))
		return;

	if (INTEL_GEN(dev_priv) >= 12) {
		intel_dp->regs.dp_tp_ctl = TGL_DP_TP_CTL(cpu_transcoder);
		intel_dp->regs.dp_tp_status = TGL_DP_TP_STATUS(cpu_transcoder);
	}

	intel_dsc_get_config(encoder, pipe_config);

	temp = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));
	if (temp & TRANS_DDI_PHSYNC)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;
	if (temp & TRANS_DDI_PVSYNC)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;

	pipe_config->hw.adjusted_mode.flags |= flags;

	switch (temp & TRANS_DDI_BPC_MASK) {
	case TRANS_DDI_BPC_6:
		pipe_config->pipe_bpp = 18;
		break;
	case TRANS_DDI_BPC_8:
		pipe_config->pipe_bpp = 24;
		break;
	case TRANS_DDI_BPC_10:
		pipe_config->pipe_bpp = 30;
		break;
	case TRANS_DDI_BPC_12:
		pipe_config->pipe_bpp = 36;
		break;
	default:
		break;
	}

	switch (temp & TRANS_DDI_MODE_SELECT_MASK) {
	case TRANS_DDI_MODE_SELECT_HDMI:
		pipe_config->has_hdmi_sink = true;

		pipe_config->infoframes.enable |=
			intel_hdmi_infoframes_enabled(encoder, pipe_config);

		if (pipe_config->infoframes.enable)
			pipe_config->has_infoframe = true;

		if (temp & TRANS_DDI_HDMI_SCRAMBLING)
			pipe_config->hdmi_scrambling = true;
		if (temp & TRANS_DDI_HIGH_TMDS_CHAR_RATE)
			pipe_config->hdmi_high_tmds_clock_ratio = true;
		/* fall through */
	case TRANS_DDI_MODE_SELECT_DVI:
		pipe_config->output_types |= BIT(INTEL_OUTPUT_HDMI);
		pipe_config->lane_count = 4;
		break;
	case TRANS_DDI_MODE_SELECT_FDI:
		pipe_config->output_types |= BIT(INTEL_OUTPUT_ANALOG);
		break;
	case TRANS_DDI_MODE_SELECT_DP_SST:
		if (encoder->type == INTEL_OUTPUT_EDP)
			pipe_config->output_types |= BIT(INTEL_OUTPUT_EDP);
		else
			pipe_config->output_types |= BIT(INTEL_OUTPUT_DP);
		pipe_config->lane_count =
			((temp & DDI_PORT_WIDTH_MASK) >> DDI_PORT_WIDTH_SHIFT) + 1;
		intel_dp_get_m_n(intel_crtc, pipe_config);

		if (INTEL_GEN(dev_priv) >= 11) {
			i915_reg_t dp_tp_ctl;

			if (IS_GEN(dev_priv, 11))
				dp_tp_ctl = DP_TP_CTL(encoder->port);
			else
				dp_tp_ctl = TGL_DP_TP_CTL(pipe_config->cpu_transcoder);

			pipe_config->fec_enable =
				intel_de_read(dev_priv, dp_tp_ctl) & DP_TP_CTL_FEC_ENABLE;

			DRM_DEBUG_KMS("[ENCODER:%d:%s] Fec status: %u\n",
				      encoder->base.base.id, encoder->base.name,
				      pipe_config->fec_enable);
		}

		break;
	case TRANS_DDI_MODE_SELECT_DP_MST:
		pipe_config->output_types |= BIT(INTEL_OUTPUT_DP_MST);
		pipe_config->lane_count =
			((temp & DDI_PORT_WIDTH_MASK) >> DDI_PORT_WIDTH_SHIFT) + 1;

		if (INTEL_GEN(dev_priv) >= 12)
			pipe_config->mst_master_transcoder =
					REG_FIELD_GET(TRANS_DDI_MST_TRANSPORT_SELECT_MASK, temp);

		intel_dp_get_m_n(intel_crtc, pipe_config);
		break;
	default:
		break;
	}

	pipe_config->has_audio =
		intel_ddi_is_audio_enabled(dev_priv, cpu_transcoder);

	if (encoder->type == INTEL_OUTPUT_EDP && dev_priv->vbt.edp.bpp &&
	    pipe_config->pipe_bpp > dev_priv->vbt.edp.bpp) {
		/*
		 * This is a big fat ugly hack.
		 *
		 * Some machines in UEFI boot mode provide us a VBT that has 18
		 * bpp and 1.62 GHz link bandwidth for eDP, which for reasons
		 * unknown we fail to light up. Yet the same BIOS boots up with
		 * 24 bpp and 2.7 GHz link. Use the same bpp as the BIOS uses as
		 * max, not what it tells us to use.
		 *
		 * Note: This will still be broken if the eDP panel is not lit
		 * up by the BIOS, and thus we can't get the mode at module
		 * load.
		 */
		DRM_DEBUG_KMS("pipe has %d bpp for eDP panel, overriding BIOS-provided max %d bpp\n",
			      pipe_config->pipe_bpp, dev_priv->vbt.edp.bpp);
		dev_priv->vbt.edp.bpp = pipe_config->pipe_bpp;
	}

	intel_ddi_clock_get(encoder, pipe_config);

	if (IS_GEN9_LP(dev_priv))
		pipe_config->lane_lat_optim_mask =
			bxt_ddi_phy_get_lane_lat_optim_mask(encoder);

	intel_ddi_compute_min_voltage_level(dev_priv, pipe_config);

	intel_hdmi_read_gcp_infoframe(encoder, pipe_config);

	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_AVI,
			     &pipe_config->infoframes.avi);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_SPD,
			     &pipe_config->infoframes.spd);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_VENDOR,
			     &pipe_config->infoframes.hdmi);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_DRM,
			     &pipe_config->infoframes.drm);
}

static enum intel_output_type
intel_ddi_compute_output_type(struct intel_encoder *encoder,
			      struct intel_crtc_state *crtc_state,
			      struct drm_connector_state *conn_state)
{
	switch (conn_state->connector->connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		return INTEL_OUTPUT_HDMI;
	case DRM_MODE_CONNECTOR_eDP:
		return INTEL_OUTPUT_EDP;
	case DRM_MODE_CONNECTOR_DisplayPort:
		return INTEL_OUTPUT_DP;
	default:
		MISSING_CASE(conn_state->connector->connector_type);
		return INTEL_OUTPUT_UNUSED;
	}
}

static int intel_ddi_compute_config(struct intel_encoder *encoder,
				    struct intel_crtc_state *pipe_config,
				    struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	int ret;

	if (HAS_TRANSCODER_EDP(dev_priv) && port == PORT_A)
		pipe_config->cpu_transcoder = TRANSCODER_EDP;

	if (intel_crtc_has_type(pipe_config, INTEL_OUTPUT_HDMI)) {
		ret = intel_hdmi_compute_config(encoder, pipe_config, conn_state);
	} else {
		ret = intel_dp_compute_config(encoder, pipe_config, conn_state);
	}

	if (ret)
		return ret;

	if (IS_HASWELL(dev_priv) && crtc->pipe == PIPE_A &&
	    pipe_config->cpu_transcoder == TRANSCODER_EDP)
		pipe_config->pch_pfit.force_thru =
			pipe_config->pch_pfit.enabled ||
			pipe_config->crc_enabled;

	if (IS_GEN9_LP(dev_priv))
		pipe_config->lane_lat_optim_mask =
			bxt_ddi_phy_calc_lane_lat_optim_mask(pipe_config->lane_count);

	intel_ddi_compute_min_voltage_level(dev_priv, pipe_config);

	return 0;
}

static bool mode_equal(const struct drm_display_mode *mode1,
		       const struct drm_display_mode *mode2)
{
	return drm_mode_match(mode1, mode2,
			      DRM_MODE_MATCH_TIMINGS |
			      DRM_MODE_MATCH_FLAGS |
			      DRM_MODE_MATCH_3D_FLAGS) &&
		mode1->clock == mode2->clock; /* we want an exact match */
}

static bool m_n_equal(const struct intel_link_m_n *m_n_1,
		      const struct intel_link_m_n *m_n_2)
{
	return m_n_1->tu == m_n_2->tu &&
		m_n_1->gmch_m == m_n_2->gmch_m &&
		m_n_1->gmch_n == m_n_2->gmch_n &&
		m_n_1->link_m == m_n_2->link_m &&
		m_n_1->link_n == m_n_2->link_n;
}

static bool crtcs_port_sync_compatible(const struct intel_crtc_state *crtc_state1,
				       const struct intel_crtc_state *crtc_state2)
{
	return crtc_state1->hw.active && crtc_state2->hw.active &&
		crtc_state1->output_types == crtc_state2->output_types &&
		crtc_state1->output_format == crtc_state2->output_format &&
		crtc_state1->lane_count == crtc_state2->lane_count &&
		crtc_state1->port_clock == crtc_state2->port_clock &&
		mode_equal(&crtc_state1->hw.adjusted_mode,
			   &crtc_state2->hw.adjusted_mode) &&
		m_n_equal(&crtc_state1->dp_m_n, &crtc_state2->dp_m_n);
}

static u8
intel_ddi_port_sync_transcoders(const struct intel_crtc_state *ref_crtc_state,
				int tile_group_id)
{
	struct drm_connector *connector;
	const struct drm_connector_state *conn_state;
	struct drm_i915_private *dev_priv = to_i915(ref_crtc_state->uapi.crtc->dev);
	struct intel_atomic_state *state =
		to_intel_atomic_state(ref_crtc_state->uapi.state);
	u8 transcoders = 0;
	int i;

	if (INTEL_GEN(dev_priv) < 11)
		return 0;

	if (!intel_crtc_has_type(ref_crtc_state, INTEL_OUTPUT_DP))
		return 0;

	for_each_new_connector_in_state(&state->base, connector, conn_state, i) {
		struct intel_crtc *crtc = to_intel_crtc(conn_state->crtc);
		const struct intel_crtc_state *crtc_state;

		if (!crtc)
			continue;

		if (!connector->has_tile ||
		    connector->tile_group->id !=
		    tile_group_id)
			continue;
		crtc_state = intel_atomic_get_new_crtc_state(state,
							     crtc);
		if (!crtcs_port_sync_compatible(ref_crtc_state,
						crtc_state))
			continue;
		transcoders |= BIT(crtc_state->cpu_transcoder);
	}

	return transcoders;
}

static int intel_ddi_compute_config_late(struct intel_encoder *encoder,
					 struct intel_crtc_state *crtc_state,
					 struct drm_connector_state *conn_state)
{
	struct drm_connector *connector = conn_state->connector;
	u8 port_sync_transcoders = 0;

	DRM_DEBUG_KMS("[ENCODER:%d:%s] [CRTC:%d:%s]",
		      encoder->base.base.id, encoder->base.name,
		      crtc_state->uapi.crtc->base.id, crtc_state->uapi.crtc->name);

	if (connector->has_tile)
		port_sync_transcoders = intel_ddi_port_sync_transcoders(crtc_state,
									connector->tile_group->id);

	/*
	 * EDP Transcoders cannot be ensalved
	 * make them a master always when present
	 */
	if (port_sync_transcoders & BIT(TRANSCODER_EDP))
		crtc_state->master_transcoder = TRANSCODER_EDP;
	else
		crtc_state->master_transcoder = ffs(port_sync_transcoders) - 1;

	if (crtc_state->master_transcoder == crtc_state->cpu_transcoder) {
		crtc_state->master_transcoder = INVALID_TRANSCODER;
		crtc_state->sync_mode_slaves_mask =
			port_sync_transcoders & ~BIT(crtc_state->cpu_transcoder);
	}

	return 0;
}

static void intel_ddi_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(to_intel_encoder(encoder));

	intel_dp_encoder_flush_work(encoder);

	drm_encoder_cleanup(encoder);
	kfree(dig_port);
}

static const struct drm_encoder_funcs intel_ddi_funcs = {
	.reset = intel_dp_encoder_reset,
	.destroy = intel_ddi_encoder_destroy,
};

static struct intel_connector *
intel_ddi_init_dp_connector(struct intel_digital_port *intel_dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dig_port->base.base.dev);
	struct intel_connector *connector;
	enum port port = intel_dig_port->base.port;

	connector = intel_connector_alloc();
	if (!connector)
		return NULL;

	intel_dig_port->dp.output_reg = DDI_BUF_CTL(port);
	intel_dig_port->dp.prepare_link_retrain =
		intel_ddi_prepare_link_retrain;
	if (INTEL_GEN(dev_priv) < 12) {
		intel_dig_port->dp.regs.dp_tp_ctl = DP_TP_CTL(port);
		intel_dig_port->dp.regs.dp_tp_status = DP_TP_STATUS(port);
	}

	if (!intel_dp_init_connector(intel_dig_port, connector)) {
		kfree(connector);
		return NULL;
	}

	return connector;
}

static int modeset_pipe(struct drm_crtc *crtc,
			struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	int ret;

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto out;
	}

	crtc_state->connectors_changed = true;

	ret = drm_atomic_commit(state);
out:
	drm_atomic_state_put(state);

	return ret;
}

static int intel_hdmi_reset_link(struct intel_encoder *encoder,
				 struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_hdmi *hdmi = enc_to_intel_hdmi(encoder);
	struct intel_connector *connector = hdmi->attached_connector;
	struct i2c_adapter *adapter =
		intel_gmbus_get_adapter(dev_priv, hdmi->ddc_bus);
	struct drm_connector_state *conn_state;
	struct intel_crtc_state *crtc_state;
	struct intel_crtc *crtc;
	u8 config;
	int ret;

	if (!connector || connector->base.status != connector_status_connected)
		return 0;

	ret = drm_modeset_lock(&dev_priv->drm.mode_config.connection_mutex,
			       ctx);
	if (ret)
		return ret;

	conn_state = connector->base.state;

	crtc = to_intel_crtc(conn_state->crtc);
	if (!crtc)
		return 0;

	ret = drm_modeset_lock(&crtc->base.mutex, ctx);
	if (ret)
		return ret;

	crtc_state = to_intel_crtc_state(crtc->base.state);

	drm_WARN_ON(&dev_priv->drm,
		    !intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI));

	if (!crtc_state->hw.active)
		return 0;

	if (!crtc_state->hdmi_high_tmds_clock_ratio &&
	    !crtc_state->hdmi_scrambling)
		return 0;

	if (conn_state->commit &&
	    !try_wait_for_completion(&conn_state->commit->hw_done))
		return 0;

	ret = drm_scdc_readb(adapter, SCDC_TMDS_CONFIG, &config);
	if (ret < 0) {
		DRM_ERROR("Failed to read TMDS config: %d\n", ret);
		return 0;
	}

	if (!!(config & SCDC_TMDS_BIT_CLOCK_RATIO_BY_40) ==
	    crtc_state->hdmi_high_tmds_clock_ratio &&
	    !!(config & SCDC_SCRAMBLING_ENABLE) ==
	    crtc_state->hdmi_scrambling)
		return 0;

	/*
	 * HDMI 2.0 says that one should not send scrambled data
	 * prior to configuring the sink scrambling, and that
	 * TMDS clock/data transmission should be suspended when
	 * changing the TMDS clock rate in the sink. So let's
	 * just do a full modeset here, even though some sinks
	 * would be perfectly happy if were to just reconfigure
	 * the SCDC settings on the fly.
	 */
	return modeset_pipe(&crtc->base, ctx);
}

static enum intel_hotplug_state
intel_ddi_hotplug(struct intel_encoder *encoder,
		  struct intel_connector *connector,
		  bool irq_received)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_modeset_acquire_ctx ctx;
	enum intel_hotplug_state state;
	int ret;

	state = intel_encoder_hotplug(encoder, connector, irq_received);

	drm_modeset_acquire_init(&ctx, 0);

	for (;;) {
		if (connector->base.connector_type == DRM_MODE_CONNECTOR_HDMIA)
			ret = intel_hdmi_reset_link(encoder, &ctx);
		else
			ret = intel_dp_retrain_link(encoder, &ctx);

		if (ret == -EDEADLK) {
			drm_modeset_backoff(&ctx);
			continue;
		}

		break;
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	drm_WARN(encoder->base.dev, ret,
		 "Acquiring modeset locks failed with %i\n", ret);

	/*
	 * Unpowered type-c dongles can take some time to boot and be
	 * responsible, so here giving some time to those dongles to power up
	 * and then retrying the probe.
	 *
	 * On many platforms the HDMI live state signal is known to be
	 * unreliable, so we can't use it to detect if a sink is connected or
	 * not. Instead we detect if it's connected based on whether we can
	 * read the EDID or not. That in turn has a problem during disconnect,
	 * since the HPD interrupt may be raised before the DDC lines get
	 * disconnected (due to how the required length of DDC vs. HPD
	 * connector pins are specified) and so we'll still be able to get a
	 * valid EDID. To solve this schedule another detection cycle if this
	 * time around we didn't detect any change in the sink's connection
	 * status.
	 */
	if (state == INTEL_HOTPLUG_UNCHANGED && irq_received &&
	    !dig_port->dp.is_mst)
		state = INTEL_HOTPLUG_RETRY;

	return state;
}

static struct intel_connector *
intel_ddi_init_hdmi_connector(struct intel_digital_port *intel_dig_port)
{
	struct intel_connector *connector;
	enum port port = intel_dig_port->base.port;

	connector = intel_connector_alloc();
	if (!connector)
		return NULL;

	intel_dig_port->hdmi.hdmi_reg = DDI_BUF_CTL(port);
	intel_hdmi_init_connector(intel_dig_port, connector);

	return connector;
}

static bool intel_ddi_a_force_4_lanes(struct intel_digital_port *dport)
{
	struct drm_i915_private *dev_priv = to_i915(dport->base.base.dev);

	if (dport->base.port != PORT_A)
		return false;

	if (dport->saved_port_bits & DDI_A_4_LANES)
		return false;

	/* Broxton/Geminilake: Bspec says that DDI_A_4_LANES is the only
	 *                     supported configuration
	 */
	if (IS_GEN9_LP(dev_priv))
		return true;

	/* Cannonlake: Most of SKUs don't support DDI_E, and the only
	 *             one who does also have a full A/E split called
	 *             DDI_F what makes DDI_E useless. However for this
	 *             case let's trust VBT info.
	 */
	if (IS_CANNONLAKE(dev_priv) &&
	    !intel_bios_is_port_present(dev_priv, PORT_E))
		return true;

	return false;
}

static int
intel_ddi_max_lanes(struct intel_digital_port *intel_dport)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dport->base.base.dev);
	enum port port = intel_dport->base.port;
	int max_lanes = 4;

	if (INTEL_GEN(dev_priv) >= 11)
		return max_lanes;

	if (port == PORT_A || port == PORT_E) {
		if (intel_de_read(dev_priv, DDI_BUF_CTL(PORT_A)) & DDI_A_4_LANES)
			max_lanes = port == PORT_A ? 4 : 0;
		else
			/* Both A and E share 2 lanes */
			max_lanes = 2;
	}

	/*
	 * Some BIOS might fail to set this bit on port A if eDP
	 * wasn't lit up at boot.  Force this bit set when needed
	 * so we use the proper lane count for our calculations.
	 */
	if (intel_ddi_a_force_4_lanes(intel_dport)) {
		DRM_DEBUG_KMS("Forcing DDI_A_4_LANES for port A\n");
		intel_dport->saved_port_bits |= DDI_A_4_LANES;
		max_lanes = 4;
	}

	return max_lanes;
}

void intel_ddi_init(struct drm_i915_private *dev_priv, enum port port)
{
	struct intel_digital_port *intel_dig_port;
	struct intel_encoder *encoder;
	bool init_hdmi, init_dp, init_lspcon = false;
	enum phy phy = intel_port_to_phy(dev_priv, port);

	init_hdmi = intel_bios_port_supports_dvi(dev_priv, port) ||
		intel_bios_port_supports_hdmi(dev_priv, port);
	init_dp = intel_bios_port_supports_dp(dev_priv, port);

	if (intel_bios_is_lspcon_present(dev_priv, port)) {
		/*
		 * Lspcon device needs to be driven with DP connector
		 * with special detection sequence. So make sure DP
		 * is initialized before lspcon.
		 */
		init_dp = true;
		init_lspcon = true;
		init_hdmi = false;
		DRM_DEBUG_KMS("VBT says port %c has lspcon\n", port_name(port));
	}

	if (!init_dp && !init_hdmi) {
		DRM_DEBUG_KMS("VBT says port %c is not DVI/HDMI/DP compatible, respect it\n",
			      port_name(port));
		return;
	}

	intel_dig_port = kzalloc(sizeof(*intel_dig_port), GFP_KERNEL);
	if (!intel_dig_port)
		return;

	encoder = &intel_dig_port->base;

	drm_encoder_init(&dev_priv->drm, &encoder->base, &intel_ddi_funcs,
			 DRM_MODE_ENCODER_TMDS, "DDI %c", port_name(port));

	encoder->hotplug = intel_ddi_hotplug;
	encoder->compute_output_type = intel_ddi_compute_output_type;
	encoder->compute_config = intel_ddi_compute_config;
	encoder->compute_config_late = intel_ddi_compute_config_late;
	encoder->enable = intel_enable_ddi;
	encoder->pre_pll_enable = intel_ddi_pre_pll_enable;
	encoder->pre_enable = intel_ddi_pre_enable;
	encoder->disable = intel_disable_ddi;
	encoder->post_disable = intel_ddi_post_disable;
	encoder->update_pipe = intel_ddi_update_pipe;
	encoder->get_hw_state = intel_ddi_get_hw_state;
	encoder->get_config = intel_ddi_get_config;
	encoder->suspend = intel_dp_encoder_suspend;
	encoder->get_power_domains = intel_ddi_get_power_domains;

	encoder->type = INTEL_OUTPUT_DDI;
	encoder->power_domain = intel_port_to_power_domain(port);
	encoder->port = port;
	encoder->cloneable = 0;
	encoder->pipe_mask = ~0;

	if (INTEL_GEN(dev_priv) >= 11)
		intel_dig_port->saved_port_bits = intel_de_read(dev_priv,
								DDI_BUF_CTL(port)) &
			DDI_BUF_PORT_REVERSAL;
	else
		intel_dig_port->saved_port_bits = intel_de_read(dev_priv,
								DDI_BUF_CTL(port)) &
			(DDI_BUF_PORT_REVERSAL | DDI_A_4_LANES);

	intel_dig_port->dp.output_reg = INVALID_MMIO_REG;
	intel_dig_port->max_lanes = intel_ddi_max_lanes(intel_dig_port);
	intel_dig_port->aux_ch = intel_bios_port_aux_ch(dev_priv, port);

	if (intel_phy_is_tc(dev_priv, phy)) {
		bool is_legacy =
			!intel_bios_port_supports_typec_usb(dev_priv, port) &&
			!intel_bios_port_supports_tbt(dev_priv, port);

		intel_tc_port_init(intel_dig_port, is_legacy);

		encoder->update_prepare = intel_ddi_update_prepare;
		encoder->update_complete = intel_ddi_update_complete;
	}

	drm_WARN_ON(&dev_priv->drm, port > PORT_I);
	intel_dig_port->ddi_io_power_domain = POWER_DOMAIN_PORT_DDI_A_IO +
					      port - PORT_A;

	if (init_dp) {
		if (!intel_ddi_init_dp_connector(intel_dig_port))
			goto err;

		intel_dig_port->hpd_pulse = intel_dp_hpd_pulse;
	}

	/* In theory we don't need the encoder->type check, but leave it just in
	 * case we have some really bad VBTs... */
	if (encoder->type != INTEL_OUTPUT_EDP && init_hdmi) {
		if (!intel_ddi_init_hdmi_connector(intel_dig_port))
			goto err;
	}

	if (init_lspcon) {
		if (lspcon_init(intel_dig_port))
			/* TODO: handle hdmi info frame part */
			DRM_DEBUG_KMS("LSPCON init success on port %c\n",
				port_name(port));
		else
			/*
			 * LSPCON init faied, but DP init was success, so
			 * lets try to drive as DP++ port.
			 */
			DRM_ERROR("LSPCON init failed on port %c\n",
				port_name(port));
	}

	intel_infoframe_init(intel_dig_port);

	return;

err:
	drm_encoder_cleanup(&encoder->base);
	kfree(intel_dig_port);
}

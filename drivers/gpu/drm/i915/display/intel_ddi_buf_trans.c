// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_ddi.h"
#include "intel_ddi_buf_trans.h"
#include "intel_de.h"
#include "intel_display_types.h"

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

static const struct cnl_ddi_buf_trans ehl_combo_phy_ddi_translations_dp[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x33, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x47, 0x36, 0x00, 0x09 },	/* 350   500      3.1   */
	{ 0xC, 0x64, 0x34, 0x00, 0x0B },	/* 350   700      6.0   */
	{ 0x6, 0x7F, 0x30, 0x00, 0x0F },	/* 350   900      8.2   */
	{ 0xA, 0x46, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x64, 0x38, 0x00, 0x07 },	/* 500   700      2.9   */
	{ 0x6, 0x7F, 0x32, 0x00, 0x0D },	/* 500   900      5.1   */
	{ 0xC, 0x61, 0x3F, 0x00, 0x00 },	/* 650   700      0.6   */
	{ 0x6, 0x7F, 0x38, 0x00, 0x07 },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

static const struct cnl_ddi_buf_trans jsl_combo_phy_ddi_translations_edp_hbr[] = {
						/* NT mV Trans mV db    */
	{ 0x8, 0x7F, 0x3F, 0x00, 0x00 },        /* 200   200      0.0   */
	{ 0x8, 0x7F, 0x38, 0x00, 0x07 },        /* 200   250      1.9   */
	{ 0x1, 0x7F, 0x33, 0x00, 0x0C },        /* 200   300      3.5   */
	{ 0xA, 0x35, 0x36, 0x00, 0x09 },        /* 200   350      4.9   */
	{ 0x8, 0x7F, 0x3F, 0x00, 0x00 },        /* 250   250      0.0   */
	{ 0x1, 0x7F, 0x38, 0x00, 0x07 },        /* 250   300      1.6   */
	{ 0xA, 0x35, 0x35, 0x00, 0x0A },        /* 250   350      2.9   */
	{ 0x1, 0x7F, 0x3F, 0x00, 0x00 },        /* 300   300      0.0   */
	{ 0xA, 0x35, 0x38, 0x00, 0x07 },        /* 300   350      1.3   */
	{ 0xA, 0x35, 0x3F, 0x00, 0x00 },        /* 350   350      0.0   */
};

static const struct cnl_ddi_buf_trans jsl_combo_phy_ddi_translations_edp_hbr2[] = {
						/* NT mV Trans mV db    */
	{ 0x8, 0x7F, 0x3F, 0x00, 0x00 },        /* 200   200      0.0   */
	{ 0x8, 0x7F, 0x3F, 0x00, 0x00 },        /* 200   250      1.9   */
	{ 0x1, 0x7F, 0x3D, 0x00, 0x02 },        /* 200   300      3.5   */
	{ 0xA, 0x35, 0x38, 0x00, 0x07 },        /* 200   350      4.9   */
	{ 0x8, 0x7F, 0x3F, 0x00, 0x00 },        /* 250   250      0.0   */
	{ 0x1, 0x7F, 0x3F, 0x00, 0x00 },        /* 250   300      1.6   */
	{ 0xA, 0x35, 0x3A, 0x00, 0x05 },        /* 250   350      2.9   */
	{ 0x1, 0x7F, 0x3F, 0x00, 0x00 },        /* 300   300      0.0   */
	{ 0xA, 0x35, 0x38, 0x00, 0x07 },        /* 300   350      1.3   */
	{ 0xA, 0x35, 0x3F, 0x00, 0x00 },        /* 350   350      0.0   */
};

static const struct cnl_ddi_buf_trans dg1_combo_phy_ddi_translations_dp_rbr_hbr[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x32, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x48, 0x35, 0x00, 0x0A },	/* 350   500      3.1   */
	{ 0xC, 0x63, 0x2F, 0x00, 0x10 },	/* 350   700      6.0   */
	{ 0x6, 0x7F, 0x2C, 0x00, 0x13 },	/* 350   900      8.2   */
	{ 0xA, 0x43, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x60, 0x36, 0x00, 0x09 },	/* 500   700      2.9   */
	{ 0x6, 0x7F, 0x30, 0x00, 0x0F },	/* 500   900      5.1   */
	{ 0xC, 0x60, 0x3F, 0x00, 0x00 },	/* 650   700      0.6   */
	{ 0x6, 0x7F, 0x37, 0x00, 0x08 },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

static const struct cnl_ddi_buf_trans dg1_combo_phy_ddi_translations_dp_hbr2_hbr3[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x32, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x48, 0x35, 0x00, 0x0A },	/* 350   500      3.1   */
	{ 0xC, 0x63, 0x2F, 0x00, 0x10 },	/* 350   700      6.0   */
	{ 0x6, 0x7F, 0x2C, 0x00, 0x13 },	/* 350   900      8.2   */
	{ 0xA, 0x43, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x60, 0x36, 0x00, 0x09 },	/* 500   700      2.9   */
	{ 0x6, 0x7F, 0x30, 0x00, 0x0F },	/* 500   900      5.1   */
	{ 0xC, 0x58, 0x3F, 0x00, 0x00 },	/* 650   700      0.6   */
	{ 0x6, 0x7F, 0x35, 0x00, 0x0A },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

static const struct icl_mg_phy_ddi_buf_trans icl_mg_phy_ddi_translations_rbr_hbr[] = {
				/* Voltage swing  pre-emphasis */
	{ 0x18, 0x00, 0x00 },	/* 0              0   */
	{ 0x1D, 0x00, 0x05 },	/* 0              1   */
	{ 0x24, 0x00, 0x0C },	/* 0              2   */
	{ 0x2B, 0x00, 0x14 },	/* 0              3   */
	{ 0x21, 0x00, 0x00 },	/* 1              0   */
	{ 0x2B, 0x00, 0x08 },	/* 1              1   */
	{ 0x30, 0x00, 0x0F },	/* 1              2   */
	{ 0x31, 0x00, 0x03 },	/* 2              0   */
	{ 0x34, 0x00, 0x0B },	/* 2              1   */
	{ 0x3F, 0x00, 0x00 },	/* 3              0   */
};

static const struct icl_mg_phy_ddi_buf_trans icl_mg_phy_ddi_translations_hbr2_hbr3[] = {
				/* Voltage swing  pre-emphasis */
	{ 0x18, 0x00, 0x00 },	/* 0              0   */
	{ 0x1D, 0x00, 0x05 },	/* 0              1   */
	{ 0x24, 0x00, 0x0C },	/* 0              2   */
	{ 0x2B, 0x00, 0x14 },	/* 0              3   */
	{ 0x26, 0x00, 0x00 },	/* 1              0   */
	{ 0x2C, 0x00, 0x07 },	/* 1              1   */
	{ 0x33, 0x00, 0x0C },	/* 1              2   */
	{ 0x2E, 0x00, 0x00 },	/* 2              0   */
	{ 0x36, 0x00, 0x09 },	/* 2              1   */
	{ 0x3F, 0x00, 0x00 },	/* 3              0   */
};

static const struct icl_mg_phy_ddi_buf_trans icl_mg_phy_ddi_translations_hdmi[] = {
				/* HDMI Preset	VS	Pre-emph */
	{ 0x1A, 0x0, 0x0 },	/* 1		400mV	0dB */
	{ 0x20, 0x0, 0x0 },	/* 2		500mV	0dB */
	{ 0x29, 0x0, 0x0 },	/* 3		650mV	0dB */
	{ 0x32, 0x0, 0x0 },	/* 4		800mV	0dB */
	{ 0x3F, 0x0, 0x0 },	/* 5		1000mV	0dB */
	{ 0x3A, 0x0, 0x5 },	/* 6		Full	-1.5 dB */
	{ 0x39, 0x0, 0x6 },	/* 7		Full	-1.8 dB */
	{ 0x38, 0x0, 0x7 },	/* 8		Full	-2 dB */
	{ 0x37, 0x0, 0x8 },	/* 9		Full	-2.5 dB */
	{ 0x36, 0x0, 0x9 },	/* 10		Full	-3 dB */
};

static const struct tgl_dkl_phy_ddi_buf_trans tgl_dkl_phy_dp_ddi_trans[] = {
				/* VS	pre-emp	Non-trans mV	Pre-emph dB */
	{ 0x7, 0x0, 0x00 },	/* 0	0	400mV		0 dB */
	{ 0x5, 0x0, 0x05 },	/* 0	1	400mV		3.5 dB */
	{ 0x2, 0x0, 0x0B },	/* 0	2	400mV		6 dB */
	{ 0x0, 0x0, 0x18 },	/* 0	3	400mV		9.5 dB */
	{ 0x5, 0x0, 0x00 },	/* 1	0	600mV		0 dB */
	{ 0x2, 0x0, 0x08 },	/* 1	1	600mV		3.5 dB */
	{ 0x0, 0x0, 0x14 },	/* 1	2	600mV		6 dB */
	{ 0x2, 0x0, 0x00 },	/* 2	0	800mV		0 dB */
	{ 0x0, 0x0, 0x0B },	/* 2	1	800mV		3.5 dB */
	{ 0x0, 0x0, 0x00 },	/* 3	0	1200mV		0 dB HDMI default */
};

static const struct tgl_dkl_phy_ddi_buf_trans tgl_dkl_phy_dp_ddi_trans_hbr2[] = {
				/* VS	pre-emp	Non-trans mV	Pre-emph dB */
	{ 0x7, 0x0, 0x00 },	/* 0	0	400mV		0 dB */
	{ 0x5, 0x0, 0x05 },	/* 0	1	400mV		3.5 dB */
	{ 0x2, 0x0, 0x0B },	/* 0	2	400mV		6 dB */
	{ 0x0, 0x0, 0x19 },	/* 0	3	400mV		9.5 dB */
	{ 0x5, 0x0, 0x00 },	/* 1	0	600mV		0 dB */
	{ 0x2, 0x0, 0x08 },	/* 1	1	600mV		3.5 dB */
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

static const struct cnl_ddi_buf_trans tgl_uy_combo_phy_ddi_translations_dp_hbr2[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x35, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x4F, 0x36, 0x00, 0x09 },	/* 350   500      3.1   */
	{ 0xC, 0x60, 0x32, 0x00, 0x0D },	/* 350   700      6.0   */
	{ 0xC, 0x7F, 0x2D, 0x00, 0x12 },	/* 350   900      8.2   */
	{ 0xC, 0x47, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x6F, 0x36, 0x00, 0x09 },	/* 500   700      2.9   */
	{ 0x6, 0x7D, 0x32, 0x00, 0x0D },	/* 500   900      5.1   */
	{ 0x6, 0x60, 0x3C, 0x00, 0x03 },	/* 650   700      0.6   */
	{ 0x6, 0x7F, 0x34, 0x00, 0x0B },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

/*
 * Cloned the HOBL entry to comply with the voltage and pre-emphasis entries
 * that DisplayPort specification requires
 */
static const struct cnl_ddi_buf_trans tgl_combo_phy_ddi_translations_edp_hbr2_hobl[] = {
						/* VS	pre-emp	*/
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 0	0	*/
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 0	1	*/
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 0	2	*/
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 0	3	*/
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 1	0	*/
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 1	1	*/
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 1	2	*/
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 2	0	*/
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 2	1	*/
};

static const struct cnl_ddi_buf_trans rkl_combo_phy_ddi_translations_dp_hbr[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x2F, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x4F, 0x37, 0x00, 0x08 },	/* 350   500      3.1   */
	{ 0xC, 0x63, 0x2F, 0x00, 0x10 },	/* 350   700      6.0   */
	{ 0x6, 0x7D, 0x2A, 0x00, 0x15 },	/* 350   900      8.2   */
	{ 0xA, 0x4C, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x73, 0x34, 0x00, 0x0B },	/* 500   700      2.9   */
	{ 0x6, 0x7F, 0x2F, 0x00, 0x10 },	/* 500   900      5.1   */
	{ 0xC, 0x6E, 0x3E, 0x00, 0x01 },	/* 650   700      0.6   */
	{ 0x6, 0x7F, 0x35, 0x00, 0x0A },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

static const struct cnl_ddi_buf_trans rkl_combo_phy_ddi_translations_dp_hbr2_hbr3[] = {
						/* NT mV Trans mV db    */
	{ 0xA, 0x35, 0x3F, 0x00, 0x00 },	/* 350   350      0.0   */
	{ 0xA, 0x50, 0x38, 0x00, 0x07 },	/* 350   500      3.1   */
	{ 0xC, 0x61, 0x33, 0x00, 0x0C },	/* 350   700      6.0   */
	{ 0x6, 0x7F, 0x2E, 0x00, 0x11 },	/* 350   900      8.2   */
	{ 0xA, 0x47, 0x3F, 0x00, 0x00 },	/* 500   500      0.0   */
	{ 0xC, 0x5F, 0x38, 0x00, 0x07 },	/* 500   700      2.9   */
	{ 0x6, 0x7F, 0x2F, 0x00, 0x10 },	/* 500   900      5.1   */
	{ 0xC, 0x5F, 0x3F, 0x00, 0x00 },	/* 650   700      0.6   */
	{ 0x6, 0x7E, 0x36, 0x00, 0x09 },	/* 600   900      3.5   */
	{ 0x6, 0x7F, 0x3F, 0x00, 0x00 },	/* 900   900      0.0   */
};

static const struct tgl_dkl_phy_ddi_buf_trans adlp_dkl_phy_dp_ddi_trans_hbr[] = {
				/* VS	pre-emp	Non-trans mV	Pre-emph dB */
	{ 0x7, 0x0, 0x01 },	/* 0	0	400mV		0 dB */
	{ 0x5, 0x0, 0x06 },	/* 0	1	400mV		3.5 dB */
	{ 0x2, 0x0, 0x0B },	/* 0	2	400mV		6 dB */
	{ 0x0, 0x0, 0x17 },	/* 0	3	400mV		9.5 dB */
	{ 0x5, 0x0, 0x00 },	/* 1	0	600mV		0 dB */
	{ 0x2, 0x0, 0x08 },	/* 1	1	600mV		3.5 dB */
	{ 0x0, 0x0, 0x14 },	/* 1	2	600mV		6 dB */
	{ 0x2, 0x0, 0x00 },	/* 2	0	800mV		0 dB */
	{ 0x0, 0x0, 0x0B },	/* 2	1	800mV		3.5 dB */
	{ 0x0, 0x0, 0x00 },	/* 3	0	1200mV		0 dB */
};

static const struct tgl_dkl_phy_ddi_buf_trans adlp_dkl_phy_dp_ddi_trans_hbr2_hbr3[] = {
				/* VS	pre-emp	Non-trans mV	Pre-emph dB */
	{ 0x7, 0x0, 0x00 },	/* 0	0	400mV		0 dB */
	{ 0x5, 0x0, 0x04 },	/* 0	1	400mV		3.5 dB */
	{ 0x2, 0x0, 0x0A },	/* 0	2	400mV		6 dB */
	{ 0x0, 0x0, 0x18 },	/* 0	3	400mV		9.5 dB */
	{ 0x5, 0x0, 0x00 },	/* 1	0	600mV		0 dB */
	{ 0x2, 0x0, 0x06 },	/* 1	1	600mV		3.5 dB */
	{ 0x0, 0x0, 0x14 },	/* 1	2	600mV		6 dB */
	{ 0x2, 0x0, 0x00 },	/* 2	0	800mV		0 dB */
	{ 0x0, 0x0, 0x09 },	/* 2	1	800mV		3.5 dB */
	{ 0x0, 0x0, 0x00 },	/* 3	0	1200mV		0 dB */
};

bool is_hobl_buf_trans(const struct cnl_ddi_buf_trans *table)
{
	return table == tgl_combo_phy_ddi_translations_edp_hbr2_hobl;
}

static const struct ddi_buf_trans *
bdw_get_buf_trans_edp(struct intel_encoder *encoder, int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (dev_priv->vbt.edp.low_vswing) {
		*n_entries = ARRAY_SIZE(bdw_ddi_translations_edp);
		return bdw_ddi_translations_edp;
	} else {
		*n_entries = ARRAY_SIZE(bdw_ddi_translations_dp);
		return bdw_ddi_translations_dp;
	}
}

static const struct ddi_buf_trans *
skl_get_buf_trans_dp(struct intel_encoder *encoder, int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

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
kbl_get_buf_trans_dp(struct intel_encoder *encoder, int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (IS_KBL_ULX(dev_priv) ||
	    IS_CFL_ULX(dev_priv) ||
	    IS_CML_ULX(dev_priv)) {
		*n_entries = ARRAY_SIZE(kbl_y_ddi_translations_dp);
		return kbl_y_ddi_translations_dp;
	} else if (IS_KBL_ULT(dev_priv) ||
		   IS_CFL_ULT(dev_priv) ||
		   IS_CML_ULT(dev_priv)) {
		*n_entries = ARRAY_SIZE(kbl_u_ddi_translations_dp);
		return kbl_u_ddi_translations_dp;
	} else {
		*n_entries = ARRAY_SIZE(kbl_ddi_translations_dp);
		return kbl_ddi_translations_dp;
	}
}

static const struct ddi_buf_trans *
skl_get_buf_trans_edp(struct intel_encoder *encoder, int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (dev_priv->vbt.edp.low_vswing) {
		if (IS_SKL_ULX(dev_priv) ||
		    IS_KBL_ULX(dev_priv) ||
		    IS_CFL_ULX(dev_priv) ||
		    IS_CML_ULX(dev_priv)) {
			*n_entries = ARRAY_SIZE(skl_y_ddi_translations_edp);
			return skl_y_ddi_translations_edp;
		} else if (IS_SKL_ULT(dev_priv) ||
			   IS_KBL_ULT(dev_priv) ||
			   IS_CFL_ULT(dev_priv) ||
			   IS_CML_ULT(dev_priv)) {
			*n_entries = ARRAY_SIZE(skl_u_ddi_translations_edp);
			return skl_u_ddi_translations_edp;
		} else {
			*n_entries = ARRAY_SIZE(skl_ddi_translations_edp);
			return skl_ddi_translations_edp;
		}
	}

	if (IS_KABYLAKE(dev_priv) ||
	    IS_COFFEELAKE(dev_priv) ||
	    IS_COMETLAKE(dev_priv))
		return kbl_get_buf_trans_dp(encoder, n_entries);
	else
		return skl_get_buf_trans_dp(encoder, n_entries);
}

static const struct ddi_buf_trans *
skl_get_buf_trans_hdmi(struct drm_i915_private *dev_priv, int *n_entries)
{
	if (IS_SKL_ULX(dev_priv) ||
	    IS_KBL_ULX(dev_priv) ||
	    IS_CFL_ULX(dev_priv) ||
	    IS_CML_ULX(dev_priv)) {
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

const struct ddi_buf_trans *
intel_ddi_get_buf_trans_dp(struct intel_encoder *encoder, int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (IS_KABYLAKE(dev_priv) ||
	    IS_COFFEELAKE(dev_priv) ||
	    IS_COMETLAKE(dev_priv)) {
		const struct ddi_buf_trans *ddi_translations =
			kbl_get_buf_trans_dp(encoder, n_entries);
		*n_entries = skl_buf_trans_num_entries(encoder->port, *n_entries);
		return ddi_translations;
	} else if (IS_SKYLAKE(dev_priv)) {
		const struct ddi_buf_trans *ddi_translations =
			skl_get_buf_trans_dp(encoder, n_entries);
		*n_entries = skl_buf_trans_num_entries(encoder->port, *n_entries);
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

const struct ddi_buf_trans *
intel_ddi_get_buf_trans_edp(struct intel_encoder *encoder, int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (DISPLAY_VER(dev_priv) == 9 && !IS_BROXTON(dev_priv)) {
		const struct ddi_buf_trans *ddi_translations =
			skl_get_buf_trans_edp(encoder, n_entries);
		*n_entries = skl_buf_trans_num_entries(encoder->port, *n_entries);
		return ddi_translations;
	} else if (IS_BROADWELL(dev_priv)) {
		return bdw_get_buf_trans_edp(encoder, n_entries);
	} else if (IS_HASWELL(dev_priv)) {
		*n_entries = ARRAY_SIZE(hsw_ddi_translations_dp);
		return hsw_ddi_translations_dp;
	}

	*n_entries = 0;
	return NULL;
}

const struct ddi_buf_trans *
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

const struct ddi_buf_trans *
intel_ddi_get_buf_trans_hdmi(struct intel_encoder *encoder,
			     int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (DISPLAY_VER(dev_priv) == 9 && !IS_BROXTON(dev_priv)) {
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
bxt_get_buf_trans_dp(struct intel_encoder *encoder, int *n_entries)
{
	*n_entries = ARRAY_SIZE(bxt_ddi_translations_dp);
	return bxt_ddi_translations_dp;
}

static const struct bxt_ddi_buf_trans *
bxt_get_buf_trans_edp(struct intel_encoder *encoder, int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (dev_priv->vbt.edp.low_vswing) {
		*n_entries = ARRAY_SIZE(bxt_ddi_translations_edp);
		return bxt_ddi_translations_edp;
	}

	return bxt_get_buf_trans_dp(encoder, n_entries);
}

static const struct bxt_ddi_buf_trans *
bxt_get_buf_trans_hdmi(struct intel_encoder *encoder, int *n_entries)
{
	*n_entries = ARRAY_SIZE(bxt_ddi_translations_hdmi);
	return bxt_ddi_translations_hdmi;
}

const struct bxt_ddi_buf_trans *
bxt_get_buf_trans(struct intel_encoder *encoder,
		  const struct intel_crtc_state *crtc_state,
		  int *n_entries)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return bxt_get_buf_trans_hdmi(encoder, n_entries);
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		return bxt_get_buf_trans_edp(encoder, n_entries);
	return bxt_get_buf_trans_dp(encoder, n_entries);
}

static const struct cnl_ddi_buf_trans *
cnl_get_buf_trans_hdmi(struct intel_encoder *encoder, int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
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
cnl_get_buf_trans_dp(struct intel_encoder *encoder, int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
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
cnl_get_buf_trans_edp(struct intel_encoder *encoder, int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
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
		return cnl_get_buf_trans_dp(encoder, n_entries);
	}
}

const struct cnl_ddi_buf_trans *
cnl_get_buf_trans(struct intel_encoder *encoder,
		  const struct intel_crtc_state *crtc_state,
		  int *n_entries)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return cnl_get_buf_trans_hdmi(encoder, n_entries);
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		return cnl_get_buf_trans_edp(encoder, n_entries);
	return cnl_get_buf_trans_dp(encoder, n_entries);
}

static const struct cnl_ddi_buf_trans *
icl_get_combo_buf_trans_hdmi(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     int *n_entries)
{
	*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_hdmi);
	return icl_combo_phy_ddi_translations_hdmi;
}

static const struct cnl_ddi_buf_trans *
icl_get_combo_buf_trans_dp(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   int *n_entries)
{
	*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_dp_hbr2);
	return icl_combo_phy_ddi_translations_dp_hbr2;
}

static const struct cnl_ddi_buf_trans *
icl_get_combo_buf_trans_edp(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (crtc_state->port_clock > 540000) {
		*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_edp_hbr3);
		return icl_combo_phy_ddi_translations_edp_hbr3;
	} else if (dev_priv->vbt.edp.low_vswing) {
		*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_edp_hbr2);
		return icl_combo_phy_ddi_translations_edp_hbr2;
	} else if (IS_DG1(dev_priv) && crtc_state->port_clock > 270000) {
		*n_entries = ARRAY_SIZE(dg1_combo_phy_ddi_translations_dp_hbr2_hbr3);
		return dg1_combo_phy_ddi_translations_dp_hbr2_hbr3;
	} else if (IS_DG1(dev_priv)) {
		*n_entries = ARRAY_SIZE(dg1_combo_phy_ddi_translations_dp_rbr_hbr);
		return dg1_combo_phy_ddi_translations_dp_rbr_hbr;
	}

	return icl_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);
}

const struct cnl_ddi_buf_trans *
icl_get_combo_buf_trans(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			int *n_entries)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return icl_get_combo_buf_trans_hdmi(encoder, crtc_state, n_entries);
	else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		return icl_get_combo_buf_trans_edp(encoder, crtc_state, n_entries);
	else
		return icl_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);
}

static const struct icl_mg_phy_ddi_buf_trans *
icl_get_mg_buf_trans_hdmi(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state,
			  int *n_entries)
{
	*n_entries = ARRAY_SIZE(icl_mg_phy_ddi_translations_hdmi);
	return icl_mg_phy_ddi_translations_hdmi;
}

static const struct icl_mg_phy_ddi_buf_trans *
icl_get_mg_buf_trans_dp(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			int *n_entries)
{
	if (crtc_state->port_clock > 270000) {
		*n_entries = ARRAY_SIZE(icl_mg_phy_ddi_translations_hbr2_hbr3);
		return icl_mg_phy_ddi_translations_hbr2_hbr3;
	} else {
		*n_entries = ARRAY_SIZE(icl_mg_phy_ddi_translations_rbr_hbr);
		return icl_mg_phy_ddi_translations_rbr_hbr;
	}
}

const struct icl_mg_phy_ddi_buf_trans *
icl_get_mg_buf_trans(struct intel_encoder *encoder,
		     const struct intel_crtc_state *crtc_state,
		     int *n_entries)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return icl_get_mg_buf_trans_hdmi(encoder, crtc_state, n_entries);
	else
		return icl_get_mg_buf_trans_dp(encoder, crtc_state, n_entries);
}

static const struct cnl_ddi_buf_trans *
ehl_get_combo_buf_trans_hdmi(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     int *n_entries)
{
	*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_hdmi);
	return icl_combo_phy_ddi_translations_hdmi;
}

static const struct cnl_ddi_buf_trans *
ehl_get_combo_buf_trans_dp(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   int *n_entries)
{
	*n_entries = ARRAY_SIZE(ehl_combo_phy_ddi_translations_dp);
	return ehl_combo_phy_ddi_translations_dp;
}

static const struct cnl_ddi_buf_trans *
ehl_get_combo_buf_trans_edp(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (dev_priv->vbt.edp.low_vswing) {
		*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_edp_hbr2);
		return icl_combo_phy_ddi_translations_edp_hbr2;
	}

	return ehl_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);
}

const struct cnl_ddi_buf_trans *
ehl_get_combo_buf_trans(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			int *n_entries)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return ehl_get_combo_buf_trans_hdmi(encoder, crtc_state, n_entries);
	else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		return ehl_get_combo_buf_trans_edp(encoder, crtc_state, n_entries);
	else
		return ehl_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);
}

static const struct cnl_ddi_buf_trans *
jsl_get_combo_buf_trans_hdmi(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     int *n_entries)
{
	*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_hdmi);
	return icl_combo_phy_ddi_translations_hdmi;
}

static const struct cnl_ddi_buf_trans *
jsl_get_combo_buf_trans_dp(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   int *n_entries)
{
	*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_dp_hbr2);
	return icl_combo_phy_ddi_translations_dp_hbr2;
}

static const struct cnl_ddi_buf_trans *
jsl_get_combo_buf_trans_edp(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (dev_priv->vbt.edp.low_vswing) {
		if (crtc_state->port_clock > 270000) {
			*n_entries = ARRAY_SIZE(jsl_combo_phy_ddi_translations_edp_hbr2);
			return jsl_combo_phy_ddi_translations_edp_hbr2;
		} else {
			*n_entries = ARRAY_SIZE(jsl_combo_phy_ddi_translations_edp_hbr);
			return jsl_combo_phy_ddi_translations_edp_hbr;
		}
	}

	return jsl_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);
}

const struct cnl_ddi_buf_trans *
jsl_get_combo_buf_trans(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			int *n_entries)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return jsl_get_combo_buf_trans_hdmi(encoder, crtc_state, n_entries);
	else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		return jsl_get_combo_buf_trans_edp(encoder, crtc_state, n_entries);
	else
		return jsl_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);
}

static const struct cnl_ddi_buf_trans *
tgl_get_combo_buf_trans_hdmi(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     int *n_entries)
{
	*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_hdmi);
	return icl_combo_phy_ddi_translations_hdmi;
}

static const struct cnl_ddi_buf_trans *
tgl_get_combo_buf_trans_dp(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (crtc_state->port_clock > 270000) {
		if (IS_ROCKETLAKE(dev_priv)) {
			*n_entries = ARRAY_SIZE(rkl_combo_phy_ddi_translations_dp_hbr2_hbr3);
			return rkl_combo_phy_ddi_translations_dp_hbr2_hbr3;
		} else if (IS_TGL_U(dev_priv) || IS_TGL_Y(dev_priv)) {
			*n_entries = ARRAY_SIZE(tgl_uy_combo_phy_ddi_translations_dp_hbr2);
			return tgl_uy_combo_phy_ddi_translations_dp_hbr2;
		} else {
			*n_entries = ARRAY_SIZE(tgl_combo_phy_ddi_translations_dp_hbr2);
			return tgl_combo_phy_ddi_translations_dp_hbr2;
		}
	} else {
		if (IS_ROCKETLAKE(dev_priv)) {
			*n_entries = ARRAY_SIZE(rkl_combo_phy_ddi_translations_dp_hbr);
			return rkl_combo_phy_ddi_translations_dp_hbr;
		} else {
			*n_entries = ARRAY_SIZE(tgl_combo_phy_ddi_translations_dp_hbr);
			return tgl_combo_phy_ddi_translations_dp_hbr;
		}
	}
}

static const struct cnl_ddi_buf_trans *
tgl_get_combo_buf_trans_edp(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    int *n_entries)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	if (crtc_state->port_clock > 540000) {
		*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_edp_hbr3);
		return icl_combo_phy_ddi_translations_edp_hbr3;
	} else if (dev_priv->vbt.edp.hobl && !intel_dp->hobl_failed) {
		*n_entries = ARRAY_SIZE(tgl_combo_phy_ddi_translations_edp_hbr2_hobl);
		return tgl_combo_phy_ddi_translations_edp_hbr2_hobl;
	} else if (dev_priv->vbt.edp.low_vswing) {
		*n_entries = ARRAY_SIZE(icl_combo_phy_ddi_translations_edp_hbr2);
		return icl_combo_phy_ddi_translations_edp_hbr2;
	}

	return tgl_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);
}

const struct cnl_ddi_buf_trans *
tgl_get_combo_buf_trans(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			int *n_entries)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return tgl_get_combo_buf_trans_hdmi(encoder, crtc_state, n_entries);
	else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		return tgl_get_combo_buf_trans_edp(encoder, crtc_state, n_entries);
	else
		return tgl_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);
}

static const struct tgl_dkl_phy_ddi_buf_trans *
tgl_get_dkl_buf_trans_hdmi(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   int *n_entries)
{
	*n_entries = ARRAY_SIZE(tgl_dkl_phy_hdmi_ddi_trans);
	return tgl_dkl_phy_hdmi_ddi_trans;
}

static const struct tgl_dkl_phy_ddi_buf_trans *
tgl_get_dkl_buf_trans_dp(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state,
			 int *n_entries)
{
	if (crtc_state->port_clock > 270000) {
		*n_entries = ARRAY_SIZE(tgl_dkl_phy_dp_ddi_trans_hbr2);
		return tgl_dkl_phy_dp_ddi_trans_hbr2;
	} else {
		*n_entries = ARRAY_SIZE(tgl_dkl_phy_dp_ddi_trans);
		return tgl_dkl_phy_dp_ddi_trans;
	}
}

const struct tgl_dkl_phy_ddi_buf_trans *
tgl_get_dkl_buf_trans(struct intel_encoder *encoder,
		      const struct intel_crtc_state *crtc_state,
		      int *n_entries)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return tgl_get_dkl_buf_trans_hdmi(encoder, crtc_state, n_entries);
	else
		return tgl_get_dkl_buf_trans_dp(encoder, crtc_state, n_entries);
}

static const struct tgl_dkl_phy_ddi_buf_trans *
adlp_get_dkl_buf_trans_dp(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state,
			  int *n_entries)
{
	if (crtc_state->port_clock > 270000) {
		*n_entries = ARRAY_SIZE(adlp_dkl_phy_dp_ddi_trans_hbr2_hbr3);
		return adlp_dkl_phy_dp_ddi_trans_hbr2_hbr3;
	}

	*n_entries = ARRAY_SIZE(adlp_dkl_phy_dp_ddi_trans_hbr);
	return adlp_dkl_phy_dp_ddi_trans_hbr;
}

const struct tgl_dkl_phy_ddi_buf_trans *
adlp_get_dkl_buf_trans(struct intel_encoder *encoder,
		       const struct intel_crtc_state *crtc_state,
		       int *n_entries)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return tgl_get_dkl_buf_trans_hdmi(encoder, crtc_state, n_entries);
	else
		return adlp_get_dkl_buf_trans_dp(encoder, crtc_state, n_entries);
}

int intel_ddi_hdmi_num_entries(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       int *default_entry)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	int n_entries;

	if (DISPLAY_VER(dev_priv) >= 12) {
		if (intel_phy_is_combo(dev_priv, phy))
			tgl_get_combo_buf_trans_hdmi(encoder, crtc_state, &n_entries);
		else
			tgl_get_dkl_buf_trans_hdmi(encoder, crtc_state, &n_entries);
		*default_entry = n_entries - 1;
	} else if (DISPLAY_VER(dev_priv) == 11) {
		if (intel_phy_is_combo(dev_priv, phy))
			icl_get_combo_buf_trans_hdmi(encoder, crtc_state, &n_entries);
		else
			icl_get_mg_buf_trans_hdmi(encoder, crtc_state, &n_entries);
		*default_entry = n_entries - 1;
	} else if (IS_CANNONLAKE(dev_priv)) {
		cnl_get_buf_trans_hdmi(encoder, &n_entries);
		*default_entry = n_entries - 1;
	} else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv)) {
		bxt_get_buf_trans_hdmi(encoder, &n_entries);
		*default_entry = n_entries - 1;
	} else if (DISPLAY_VER(dev_priv) == 9) {
		intel_ddi_get_buf_trans_hdmi(encoder, &n_entries);
		*default_entry = 8;
	} else if (IS_BROADWELL(dev_priv)) {
		intel_ddi_get_buf_trans_hdmi(encoder, &n_entries);
		*default_entry = 7;
	} else if (IS_HASWELL(dev_priv)) {
		intel_ddi_get_buf_trans_hdmi(encoder, &n_entries);
		*default_entry = 6;
	} else {
		drm_WARN(&dev_priv->drm, 1, "ddi translation table missing\n");
		return 0;
	}

	if (drm_WARN_ON_ONCE(&dev_priv->drm, n_entries == 0))
		return 0;

	return n_entries;
}

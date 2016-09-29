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

#include "i915_drv.h"
#include "intel_drv.h"

struct ddi_buf_trans {
	u32 trans1;	/* balance leg enable, de-emph level */
	u32 trans2;	/* vref sel, vswing */
	u8 i_boost;	/* SKL: I_boost; valid: 0x0, 0x1, 0x3, 0x7 */
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

/*
 * Skylake H and S
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
 * Skylake U
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
 * Skylake Y
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

/* Skylake U, H and S */
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

/* Skylake Y */
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
	u32 margin;	/* swing value */
	u32 scale;	/* scale value */
	u32 enable;	/* scale enable */
	u32 deemphasis;
	bool default_index; /* true if the entry represents default value */
};

static const struct bxt_ddi_buf_trans bxt_ddi_translations_dp[] = {
					/* Idx	NT mV diff	db  */
	{ 52,  0x9A, 0, 128, true  },	/* 0:	400		0   */
	{ 78,  0x9A, 0, 85,  false },	/* 1:	400		3.5 */
	{ 104, 0x9A, 0, 64,  false },	/* 2:	400		6   */
	{ 154, 0x9A, 0, 43,  false },	/* 3:	400		9.5 */
	{ 77,  0x9A, 0, 128, false },	/* 4:	600		0   */
	{ 116, 0x9A, 0, 85,  false },	/* 5:	600		3.5 */
	{ 154, 0x9A, 0, 64,  false },	/* 6:	600		6   */
	{ 102, 0x9A, 0, 128, false },	/* 7:	800		0   */
	{ 154, 0x9A, 0, 85,  false },	/* 8:	800		3.5 */
	{ 154, 0x9A, 1, 128, false },	/* 9:	1200		0   */
};

static const struct bxt_ddi_buf_trans bxt_ddi_translations_edp[] = {
					/* Idx	NT mV diff	db  */
	{ 26, 0, 0, 128, false },	/* 0:	200		0   */
	{ 38, 0, 0, 112, false },	/* 1:	200		1.5 */
	{ 48, 0, 0, 96,  false },	/* 2:	200		4   */
	{ 54, 0, 0, 69,  false },	/* 3:	200		6   */
	{ 32, 0, 0, 128, false },	/* 4:	250		0   */
	{ 48, 0, 0, 104, false },	/* 5:	250		1.5 */
	{ 54, 0, 0, 85,  false },	/* 6:	250		4   */
	{ 43, 0, 0, 128, false },	/* 7:	300		0   */
	{ 54, 0, 0, 101, false },	/* 8:	300		1.5 */
	{ 48, 0, 0, 128, false },	/* 9:	300		0   */
};

/* BSpec has 2 recommended values - entries 0 and 8.
 * Using the entry with higher vswing.
 */
static const struct bxt_ddi_buf_trans bxt_ddi_translations_hdmi[] = {
					/* Idx	NT mV diff	db  */
	{ 52,  0x9A, 0, 128, false },	/* 0:	400		0   */
	{ 52,  0x9A, 0, 85,  false },	/* 1:	400		3.5 */
	{ 52,  0x9A, 0, 64,  false },	/* 2:	400		6   */
	{ 42,  0x9A, 0, 43,  false },	/* 3:	400		9.5 */
	{ 77,  0x9A, 0, 128, false },	/* 4:	600		0   */
	{ 77,  0x9A, 0, 85,  false },	/* 5:	600		3.5 */
	{ 77,  0x9A, 0, 64,  false },	/* 6:	600		6   */
	{ 102, 0x9A, 0, 128, false },	/* 7:	800		0   */
	{ 102, 0x9A, 0, 85,  false },	/* 8:	800		3.5 */
	{ 154, 0x9A, 1, 128, true },	/* 9:	1200		0   */
};

enum port intel_ddi_get_encoder_port(struct intel_encoder *encoder)
{
	switch (encoder->type) {
	case INTEL_OUTPUT_DP_MST:
		return enc_to_mst(&encoder->base)->primary->port;
	case INTEL_OUTPUT_DP:
	case INTEL_OUTPUT_EDP:
	case INTEL_OUTPUT_HDMI:
	case INTEL_OUTPUT_UNKNOWN:
		return enc_to_dig_port(&encoder->base)->port;
	case INTEL_OUTPUT_ANALOG:
		return PORT_E;
	default:
		MISSING_CASE(encoder->type);
		return PORT_A;
	}
}

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
	if (IS_SKL_ULX(dev_priv) || IS_KBL_ULX(dev_priv)) {
		*n_entries = ARRAY_SIZE(skl_y_ddi_translations_dp);
		return skl_y_ddi_translations_dp;
	} else if (IS_SKL_ULT(dev_priv) || IS_KBL_ULT(dev_priv)) {
		*n_entries = ARRAY_SIZE(skl_u_ddi_translations_dp);
		return skl_u_ddi_translations_dp;
	} else {
		*n_entries = ARRAY_SIZE(skl_ddi_translations_dp);
		return skl_ddi_translations_dp;
	}
}

static const struct ddi_buf_trans *
skl_get_buf_trans_edp(struct drm_i915_private *dev_priv, int *n_entries)
{
	if (dev_priv->vbt.edp.low_vswing) {
		if (IS_SKL_ULX(dev_priv) || IS_KBL_ULX(dev_priv)) {
			*n_entries = ARRAY_SIZE(skl_y_ddi_translations_edp);
			return skl_y_ddi_translations_edp;
		} else if (IS_SKL_ULT(dev_priv) || IS_KBL_ULT(dev_priv)) {
			*n_entries = ARRAY_SIZE(skl_u_ddi_translations_edp);
			return skl_u_ddi_translations_edp;
		} else {
			*n_entries = ARRAY_SIZE(skl_ddi_translations_edp);
			return skl_ddi_translations_edp;
		}
	}

	return skl_get_buf_trans_dp(dev_priv, n_entries);
}

static const struct ddi_buf_trans *
skl_get_buf_trans_hdmi(struct drm_i915_private *dev_priv, int *n_entries)
{
	if (IS_SKL_ULX(dev_priv) || IS_KBL_ULX(dev_priv)) {
		*n_entries = ARRAY_SIZE(skl_y_ddi_translations_hdmi);
		return skl_y_ddi_translations_hdmi;
	} else {
		*n_entries = ARRAY_SIZE(skl_ddi_translations_hdmi);
		return skl_ddi_translations_hdmi;
	}
}

static int intel_ddi_hdmi_level(struct drm_i915_private *dev_priv, enum port port)
{
	int n_hdmi_entries;
	int hdmi_level;
	int hdmi_default_entry;

	hdmi_level = dev_priv->vbt.ddi_port_info[port].hdmi_level_shift;

	if (IS_BROXTON(dev_priv))
		return hdmi_level;

	if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv)) {
		skl_get_buf_trans_hdmi(dev_priv, &n_hdmi_entries);
		hdmi_default_entry = 8;
	} else if (IS_BROADWELL(dev_priv)) {
		n_hdmi_entries = ARRAY_SIZE(bdw_ddi_translations_hdmi);
		hdmi_default_entry = 7;
	} else if (IS_HASWELL(dev_priv)) {
		n_hdmi_entries = ARRAY_SIZE(hsw_ddi_translations_hdmi);
		hdmi_default_entry = 6;
	} else {
		WARN(1, "ddi translation table missing\n");
		n_hdmi_entries = ARRAY_SIZE(bdw_ddi_translations_hdmi);
		hdmi_default_entry = 7;
	}

	/* Choose a good default if VBT is badly populated */
	if (hdmi_level == HDMI_LEVEL_SHIFT_UNKNOWN ||
	    hdmi_level >= n_hdmi_entries)
		hdmi_level = hdmi_default_entry;

	return hdmi_level;
}

/*
 * Starting with Haswell, DDI port buffers must be programmed with correct
 * values in advance. This function programs the correct values for
 * DP/eDP/FDI use cases.
 */
void intel_prepare_dp_ddi_buffers(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 iboost_bit = 0;
	int i, n_dp_entries, n_edp_entries, size;
	enum port port = intel_ddi_get_encoder_port(encoder);
	const struct ddi_buf_trans *ddi_translations_fdi;
	const struct ddi_buf_trans *ddi_translations_dp;
	const struct ddi_buf_trans *ddi_translations_edp;
	const struct ddi_buf_trans *ddi_translations;

	if (IS_BROXTON(dev_priv))
		return;

	if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv)) {
		ddi_translations_fdi = NULL;
		ddi_translations_dp =
				skl_get_buf_trans_dp(dev_priv, &n_dp_entries);
		ddi_translations_edp =
				skl_get_buf_trans_edp(dev_priv, &n_edp_entries);

		/* If we're boosting the current, set bit 31 of trans1 */
		if (dev_priv->vbt.ddi_port_info[port].dp_boost_level)
			iboost_bit = DDI_BUF_BALANCE_LEG_ENABLE;

		if (WARN_ON(encoder->type == INTEL_OUTPUT_EDP &&
			    port != PORT_A && port != PORT_E &&
			    n_edp_entries > 9))
			n_edp_entries = 9;
	} else if (IS_BROADWELL(dev_priv)) {
		ddi_translations_fdi = bdw_ddi_translations_fdi;
		ddi_translations_dp = bdw_ddi_translations_dp;
		ddi_translations_edp = bdw_get_buf_trans_edp(dev_priv, &n_edp_entries);
		n_dp_entries = ARRAY_SIZE(bdw_ddi_translations_dp);
	} else if (IS_HASWELL(dev_priv)) {
		ddi_translations_fdi = hsw_ddi_translations_fdi;
		ddi_translations_dp = hsw_ddi_translations_dp;
		ddi_translations_edp = hsw_ddi_translations_dp;
		n_dp_entries = n_edp_entries = ARRAY_SIZE(hsw_ddi_translations_dp);
	} else {
		WARN(1, "ddi translation table missing\n");
		ddi_translations_edp = bdw_ddi_translations_dp;
		ddi_translations_fdi = bdw_ddi_translations_fdi;
		ddi_translations_dp = bdw_ddi_translations_dp;
		n_edp_entries = ARRAY_SIZE(bdw_ddi_translations_edp);
		n_dp_entries = ARRAY_SIZE(bdw_ddi_translations_dp);
	}

	switch (encoder->type) {
	case INTEL_OUTPUT_EDP:
		ddi_translations = ddi_translations_edp;
		size = n_edp_entries;
		break;
	case INTEL_OUTPUT_DP:
		ddi_translations = ddi_translations_dp;
		size = n_dp_entries;
		break;
	case INTEL_OUTPUT_ANALOG:
		ddi_translations = ddi_translations_fdi;
		size = n_dp_entries;
		break;
	default:
		BUG();
	}

	for (i = 0; i < size; i++) {
		I915_WRITE(DDI_BUF_TRANS_LO(port, i),
			   ddi_translations[i].trans1 | iboost_bit);
		I915_WRITE(DDI_BUF_TRANS_HI(port, i),
			   ddi_translations[i].trans2);
	}
}

/*
 * Starting with Haswell, DDI port buffers must be programmed with correct
 * values in advance. This function programs the correct values for
 * HDMI/DVI use cases.
 */
static void intel_prepare_hdmi_ddi_buffers(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 iboost_bit = 0;
	int n_hdmi_entries, hdmi_level;
	enum port port = intel_ddi_get_encoder_port(encoder);
	const struct ddi_buf_trans *ddi_translations_hdmi;

	if (IS_BROXTON(dev_priv))
		return;

	hdmi_level = intel_ddi_hdmi_level(dev_priv, port);

	if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv)) {
		ddi_translations_hdmi = skl_get_buf_trans_hdmi(dev_priv, &n_hdmi_entries);

		/* If we're boosting the current, set bit 31 of trans1 */
		if (dev_priv->vbt.ddi_port_info[port].hdmi_boost_level)
			iboost_bit = DDI_BUF_BALANCE_LEG_ENABLE;
	} else if (IS_BROADWELL(dev_priv)) {
		ddi_translations_hdmi = bdw_ddi_translations_hdmi;
		n_hdmi_entries = ARRAY_SIZE(bdw_ddi_translations_hdmi);
	} else if (IS_HASWELL(dev_priv)) {
		ddi_translations_hdmi = hsw_ddi_translations_hdmi;
		n_hdmi_entries = ARRAY_SIZE(hsw_ddi_translations_hdmi);
	} else {
		WARN(1, "ddi translation table missing\n");
		ddi_translations_hdmi = bdw_ddi_translations_hdmi;
		n_hdmi_entries = ARRAY_SIZE(bdw_ddi_translations_hdmi);
	}

	/* Entry 9 is for HDMI: */
	I915_WRITE(DDI_BUF_TRANS_LO(port, 9),
		   ddi_translations_hdmi[hdmi_level].trans1 | iboost_bit);
	I915_WRITE(DDI_BUF_TRANS_HI(port, 9),
		   ddi_translations_hdmi[hdmi_level].trans2);
}

static void intel_wait_ddi_buf_idle(struct drm_i915_private *dev_priv,
				    enum port port)
{
	i915_reg_t reg = DDI_BUF_CTL(port);
	int i;

	for (i = 0; i < 16; i++) {
		udelay(1);
		if (I915_READ(reg) & DDI_BUF_IS_IDLE)
			return;
	}
	DRM_ERROR("Timeout waiting for DDI BUF %c idle bit\n", port_name(port));
}

static uint32_t hsw_pll_to_ddi_pll_sel(struct intel_shared_dpll *pll)
{
	switch (pll->id) {
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
		MISSING_CASE(pll->id);
		return PORT_CLK_SEL_NONE;
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

void hsw_fdi_link_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *encoder;
	u32 temp, i, rx_ctl_val, ddi_pll_sel;

	for_each_encoder_on_crtc(dev, crtc, encoder) {
		WARN_ON(encoder->type != INTEL_OUTPUT_ANALOG);
		intel_prepare_dp_ddi_buffers(encoder);
	}

	/* Set the FDI_RX_MISC pwrdn lanes and the 2 workarounds listed at the
	 * mode set "sequence for CRT port" document:
	 * - TP1 to TP2 time with the default value
	 * - FDI delay to 90h
	 *
	 * WaFDIAutoLinkSetTimingOverrride:hsw
	 */
	I915_WRITE(FDI_RX_MISC(PIPE_A), FDI_RX_PWRDN_LANE1_VAL(2) |
				  FDI_RX_PWRDN_LANE0_VAL(2) |
				  FDI_RX_TP1_TO_TP2_48 | FDI_RX_FDI_DELAY_90);

	/* Enable the PCH Receiver FDI PLL */
	rx_ctl_val = dev_priv->fdi_rx_config | FDI_RX_ENHANCE_FRAME_ENABLE |
		     FDI_RX_PLL_ENABLE |
		     FDI_DP_PORT_WIDTH(intel_crtc->config->fdi_lanes);
	I915_WRITE(FDI_RX_CTL(PIPE_A), rx_ctl_val);
	POSTING_READ(FDI_RX_CTL(PIPE_A));
	udelay(220);

	/* Switch from Rawclk to PCDclk */
	rx_ctl_val |= FDI_PCDCLK;
	I915_WRITE(FDI_RX_CTL(PIPE_A), rx_ctl_val);

	/* Configure Port Clock Select */
	ddi_pll_sel = hsw_pll_to_ddi_pll_sel(intel_crtc->config->shared_dpll);
	I915_WRITE(PORT_CLK_SEL(PORT_E), ddi_pll_sel);
	WARN_ON(ddi_pll_sel != PORT_CLK_SEL_SPLL);

	/* Start the training iterating through available voltages and emphasis,
	 * testing each value twice. */
	for (i = 0; i < ARRAY_SIZE(hsw_ddi_translations_fdi) * 2; i++) {
		/* Configure DP_TP_CTL with auto-training */
		I915_WRITE(DP_TP_CTL(PORT_E),
					DP_TP_CTL_FDI_AUTOTRAIN |
					DP_TP_CTL_ENHANCED_FRAME_ENABLE |
					DP_TP_CTL_LINK_TRAIN_PAT1 |
					DP_TP_CTL_ENABLE);

		/* Configure and enable DDI_BUF_CTL for DDI E with next voltage.
		 * DDI E does not support port reversal, the functionality is
		 * achieved on the PCH side in FDI_RX_CTL, so no need to set the
		 * port reversal bit */
		I915_WRITE(DDI_BUF_CTL(PORT_E),
			   DDI_BUF_CTL_ENABLE |
			   ((intel_crtc->config->fdi_lanes - 1) << 1) |
			   DDI_BUF_TRANS_SELECT(i / 2));
		POSTING_READ(DDI_BUF_CTL(PORT_E));

		udelay(600);

		/* Program PCH FDI Receiver TU */
		I915_WRITE(FDI_RX_TUSIZE1(PIPE_A), TU_SIZE(64));

		/* Enable PCH FDI Receiver with auto-training */
		rx_ctl_val |= FDI_RX_ENABLE | FDI_LINK_TRAIN_AUTO;
		I915_WRITE(FDI_RX_CTL(PIPE_A), rx_ctl_val);
		POSTING_READ(FDI_RX_CTL(PIPE_A));

		/* Wait for FDI receiver lane calibration */
		udelay(30);

		/* Unset FDI_RX_MISC pwrdn lanes */
		temp = I915_READ(FDI_RX_MISC(PIPE_A));
		temp &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
		I915_WRITE(FDI_RX_MISC(PIPE_A), temp);
		POSTING_READ(FDI_RX_MISC(PIPE_A));

		/* Wait for FDI auto training time */
		udelay(5);

		temp = I915_READ(DP_TP_STATUS(PORT_E));
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
		I915_WRITE(FDI_RX_CTL(PIPE_A), rx_ctl_val);
		POSTING_READ(FDI_RX_CTL(PIPE_A));

		temp = I915_READ(DDI_BUF_CTL(PORT_E));
		temp &= ~DDI_BUF_CTL_ENABLE;
		I915_WRITE(DDI_BUF_CTL(PORT_E), temp);
		POSTING_READ(DDI_BUF_CTL(PORT_E));

		/* Disable DP_TP_CTL and FDI_RX_CTL and retry */
		temp = I915_READ(DP_TP_CTL(PORT_E));
		temp &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
		temp |= DP_TP_CTL_LINK_TRAIN_PAT1;
		I915_WRITE(DP_TP_CTL(PORT_E), temp);
		POSTING_READ(DP_TP_CTL(PORT_E));

		intel_wait_ddi_buf_idle(dev_priv, PORT_E);

		/* Reset FDI_RX_MISC pwrdn lanes */
		temp = I915_READ(FDI_RX_MISC(PIPE_A));
		temp &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
		temp |= FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2);
		I915_WRITE(FDI_RX_MISC(PIPE_A), temp);
		POSTING_READ(FDI_RX_MISC(PIPE_A));
	}

	/* Enable normal pixel sending for FDI */
	I915_WRITE(DP_TP_CTL(PORT_E),
		   DP_TP_CTL_FDI_AUTOTRAIN |
		   DP_TP_CTL_LINK_TRAIN_NORMAL |
		   DP_TP_CTL_ENHANCED_FRAME_ENABLE |
		   DP_TP_CTL_ENABLE);
}

void intel_ddi_init_dp_buf_reg(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_digital_port *intel_dig_port =
		enc_to_dig_port(&encoder->base);

	intel_dp->DP = intel_dig_port->saved_port_bits |
		DDI_BUF_CTL_ENABLE | DDI_BUF_TRANS_SELECT(0);
	intel_dp->DP |= DDI_PORT_WIDTH(intel_dp->lane_count);
}

static struct intel_encoder *
intel_ddi_get_crtc_encoder(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *intel_encoder, *ret = NULL;
	int num_encoders = 0;

	for_each_encoder_on_crtc(dev, crtc, intel_encoder) {
		ret = intel_encoder;
		num_encoders++;
	}

	if (num_encoders != 1)
		WARN(1, "%d encoders on crtc for pipe %c\n", num_encoders,
		     pipe_name(intel_crtc->pipe));

	BUG_ON(ret == NULL);
	return ret;
}

struct intel_encoder *
intel_ddi_get_crtc_new_encoder(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct intel_encoder *ret = NULL;
	struct drm_atomic_state *state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int num_encoders = 0;
	int i;

	state = crtc_state->base.state;

	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != crtc_state->base.crtc)
			continue;

		ret = to_intel_encoder(connector_state->best_encoder);
		num_encoders++;
	}

	WARN(num_encoders != 1, "%d encoders on crtc for pipe %c\n", num_encoders,
	     pipe_name(crtc->pipe));

	BUG_ON(ret == NULL);
	return ret;
}

#define LC_FREQ 2700

static int hsw_ddi_calc_wrpll_link(struct drm_i915_private *dev_priv,
				   i915_reg_t reg)
{
	int refclk = LC_FREQ;
	int n, p, r;
	u32 wrpll;

	wrpll = I915_READ(reg);
	switch (wrpll & WRPLL_PLL_REF_MASK) {
	case WRPLL_PLL_SSC:
	case WRPLL_PLL_NON_SSC:
		/*
		 * We could calculate spread here, but our checking
		 * code only cares about 5% accuracy, and spread is a max of
		 * 0.5% downspread.
		 */
		refclk = 135;
		break;
	case WRPLL_PLL_LCPLL:
		refclk = LC_FREQ;
		break;
	default:
		WARN(1, "bad wrpll refclk\n");
		return 0;
	}

	r = wrpll & WRPLL_DIVIDER_REF_MASK;
	p = (wrpll & WRPLL_DIVIDER_POST_MASK) >> WRPLL_DIVIDER_POST_SHIFT;
	n = (wrpll & WRPLL_DIVIDER_FB_MASK) >> WRPLL_DIVIDER_FB_SHIFT;

	/* Convert to KHz, p & r have a fixed point portion */
	return (refclk * n * 100) / (p * r);
}

static int skl_calc_wrpll_link(struct drm_i915_private *dev_priv,
			       uint32_t dpll)
{
	i915_reg_t cfgcr1_reg, cfgcr2_reg;
	uint32_t cfgcr1_val, cfgcr2_val;
	uint32_t p0, p1, p2, dco_freq;

	cfgcr1_reg = DPLL_CFGCR1(dpll);
	cfgcr2_reg = DPLL_CFGCR2(dpll);

	cfgcr1_val = I915_READ(cfgcr1_reg);
	cfgcr2_val = I915_READ(cfgcr2_reg);

	p0 = cfgcr2_val & DPLL_CFGCR2_PDIV_MASK;
	p2 = cfgcr2_val & DPLL_CFGCR2_KDIV_MASK;

	if (cfgcr2_val &  DPLL_CFGCR2_QDIV_MODE(1))
		p1 = (cfgcr2_val & DPLL_CFGCR2_QDIV_RATIO_MASK) >> 8;
	else
		p1 = 1;


	switch (p0) {
	case DPLL_CFGCR2_PDIV_1:
		p0 = 1;
		break;
	case DPLL_CFGCR2_PDIV_2:
		p0 = 2;
		break;
	case DPLL_CFGCR2_PDIV_3:
		p0 = 3;
		break;
	case DPLL_CFGCR2_PDIV_7:
		p0 = 7;
		break;
	}

	switch (p2) {
	case DPLL_CFGCR2_KDIV_5:
		p2 = 5;
		break;
	case DPLL_CFGCR2_KDIV_2:
		p2 = 2;
		break;
	case DPLL_CFGCR2_KDIV_3:
		p2 = 3;
		break;
	case DPLL_CFGCR2_KDIV_1:
		p2 = 1;
		break;
	}

	dco_freq = (cfgcr1_val & DPLL_CFGCR1_DCO_INTEGER_MASK) * 24 * 1000;

	dco_freq += (((cfgcr1_val & DPLL_CFGCR1_DCO_FRACTION_MASK) >> 9) * 24 *
		1000) / 0x8000;

	return dco_freq / (p0 * p1 * p2 * 5);
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
	else if (pipe_config->has_hdmi_sink && pipe_config->pipe_bpp == 36)
		dotclock = pipe_config->port_clock * 2 / 3;
	else
		dotclock = pipe_config->port_clock;

	if (pipe_config->pixel_multiplier)
		dotclock /= pipe_config->pixel_multiplier;

	pipe_config->base.adjusted_mode.crtc_clock = dotclock;
}

static void skl_ddi_clock_get(struct intel_encoder *encoder,
				struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int link_clock = 0;
	uint32_t dpll_ctl1, dpll;

	dpll = intel_get_shared_dpll_id(dev_priv, pipe_config->shared_dpll);

	dpll_ctl1 = I915_READ(DPLL_CTRL1);

	if (dpll_ctl1 & DPLL_CTRL1_HDMI_MODE(dpll)) {
		link_clock = skl_calc_wrpll_link(dev_priv, dpll);
	} else {
		link_clock = dpll_ctl1 & DPLL_CTRL1_LINK_RATE_MASK(dpll);
		link_clock >>= DPLL_CTRL1_LINK_RATE_SHIFT(dpll);

		switch (link_clock) {
		case DPLL_CTRL1_LINK_RATE_810:
			link_clock = 81000;
			break;
		case DPLL_CTRL1_LINK_RATE_1080:
			link_clock = 108000;
			break;
		case DPLL_CTRL1_LINK_RATE_1350:
			link_clock = 135000;
			break;
		case DPLL_CTRL1_LINK_RATE_1620:
			link_clock = 162000;
			break;
		case DPLL_CTRL1_LINK_RATE_2160:
			link_clock = 216000;
			break;
		case DPLL_CTRL1_LINK_RATE_2700:
			link_clock = 270000;
			break;
		default:
			WARN(1, "Unsupported link rate\n");
			break;
		}
		link_clock *= 2;
	}

	pipe_config->port_clock = link_clock;

	ddi_dotclock_get(pipe_config);
}

static void hsw_ddi_clock_get(struct intel_encoder *encoder,
			      struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int link_clock = 0;
	u32 val, pll;

	val = hsw_pll_to_ddi_pll_sel(pipe_config->shared_dpll);
	switch (val & PORT_CLK_SEL_MASK) {
	case PORT_CLK_SEL_LCPLL_810:
		link_clock = 81000;
		break;
	case PORT_CLK_SEL_LCPLL_1350:
		link_clock = 135000;
		break;
	case PORT_CLK_SEL_LCPLL_2700:
		link_clock = 270000;
		break;
	case PORT_CLK_SEL_WRPLL1:
		link_clock = hsw_ddi_calc_wrpll_link(dev_priv, WRPLL_CTL(0));
		break;
	case PORT_CLK_SEL_WRPLL2:
		link_clock = hsw_ddi_calc_wrpll_link(dev_priv, WRPLL_CTL(1));
		break;
	case PORT_CLK_SEL_SPLL:
		pll = I915_READ(SPLL_CTL) & SPLL_PLL_FREQ_MASK;
		if (pll == SPLL_PLL_FREQ_810MHz)
			link_clock = 81000;
		else if (pll == SPLL_PLL_FREQ_1350MHz)
			link_clock = 135000;
		else if (pll == SPLL_PLL_FREQ_2700MHz)
			link_clock = 270000;
		else {
			WARN(1, "bad spll freq\n");
			return;
		}
		break;
	default:
		WARN(1, "bad port clock sel\n");
		return;
	}

	pipe_config->port_clock = link_clock * 2;

	ddi_dotclock_get(pipe_config);
}

static int bxt_calc_pll_link(struct drm_i915_private *dev_priv,
				enum intel_dpll_id dpll)
{
	struct intel_shared_dpll *pll;
	struct intel_dpll_hw_state *state;
	struct dpll clock;

	/* For DDI ports we always use a shared PLL. */
	if (WARN_ON(dpll == DPLL_ID_PRIVATE))
		return 0;

	pll = &dev_priv->shared_dplls[dpll];
	state = &pll->config.hw_state;

	clock.m1 = 2;
	clock.m2 = (state->pll0 & PORT_PLL_M2_MASK) << 22;
	if (state->pll3 & PORT_PLL_M2_FRAC_ENABLE)
		clock.m2 |= state->pll2 & PORT_PLL_M2_FRAC_MASK;
	clock.n = (state->pll1 & PORT_PLL_N_MASK) >> PORT_PLL_N_SHIFT;
	clock.p1 = (state->ebb0 & PORT_PLL_P1_MASK) >> PORT_PLL_P1_SHIFT;
	clock.p2 = (state->ebb0 & PORT_PLL_P2_MASK) >> PORT_PLL_P2_SHIFT;

	return chv_calc_dpll_params(100000, &clock);
}

static void bxt_ddi_clock_get(struct intel_encoder *encoder,
				struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = intel_ddi_get_encoder_port(encoder);
	uint32_t dpll = port;

	pipe_config->port_clock = bxt_calc_pll_link(dev_priv, dpll);

	ddi_dotclock_get(pipe_config);
}

void intel_ddi_clock_get(struct intel_encoder *encoder,
			 struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = encoder->base.dev;

	if (INTEL_INFO(dev)->gen <= 8)
		hsw_ddi_clock_get(encoder, pipe_config);
	else if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev))
		skl_ddi_clock_get(encoder, pipe_config);
	else if (IS_BROXTON(dev))
		bxt_ddi_clock_get(encoder, pipe_config);
}

static bool
hsw_ddi_pll_select(struct intel_crtc *intel_crtc,
		   struct intel_crtc_state *crtc_state,
		   struct intel_encoder *intel_encoder)
{
	struct intel_shared_dpll *pll;

	pll = intel_get_shared_dpll(intel_crtc, crtc_state,
				    intel_encoder);
	if (!pll)
		DRM_DEBUG_DRIVER("failed to find PLL for pipe %c\n",
				 pipe_name(intel_crtc->pipe));

	return pll;
}

static bool
skl_ddi_pll_select(struct intel_crtc *intel_crtc,
		   struct intel_crtc_state *crtc_state,
		   struct intel_encoder *intel_encoder)
{
	struct intel_shared_dpll *pll;

	pll = intel_get_shared_dpll(intel_crtc, crtc_state, intel_encoder);
	if (pll == NULL) {
		DRM_DEBUG_DRIVER("failed to find PLL for pipe %c\n",
				 pipe_name(intel_crtc->pipe));
		return false;
	}

	return true;
}

static bool
bxt_ddi_pll_select(struct intel_crtc *intel_crtc,
		   struct intel_crtc_state *crtc_state,
		   struct intel_encoder *intel_encoder)
{
	return !!intel_get_shared_dpll(intel_crtc, crtc_state, intel_encoder);
}

/*
 * Tries to find a *shared* PLL for the CRTC and store it in
 * intel_crtc->ddi_pll_sel.
 *
 * For private DPLLs, compute_config() should do the selection for us. This
 * function should be folded into compute_config() eventually.
 */
bool intel_ddi_pll_select(struct intel_crtc *intel_crtc,
			  struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct intel_encoder *intel_encoder =
		intel_ddi_get_crtc_new_encoder(crtc_state);

	if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev))
		return skl_ddi_pll_select(intel_crtc, crtc_state,
					  intel_encoder);
	else if (IS_BROXTON(dev))
		return bxt_ddi_pll_select(intel_crtc, crtc_state,
					  intel_encoder);
	else
		return hsw_ddi_pll_select(intel_crtc, crtc_state,
					  intel_encoder);
}

void intel_ddi_set_pipe_settings(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *intel_encoder = intel_ddi_get_crtc_encoder(crtc);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	int type = intel_encoder->type;
	uint32_t temp;

	if (type == INTEL_OUTPUT_DP || type == INTEL_OUTPUT_EDP || type == INTEL_OUTPUT_DP_MST) {
		WARN_ON(transcoder_is_dsi(cpu_transcoder));

		temp = TRANS_MSA_SYNC_CLK;
		switch (intel_crtc->config->pipe_bpp) {
		case 18:
			temp |= TRANS_MSA_6_BPC;
			break;
		case 24:
			temp |= TRANS_MSA_8_BPC;
			break;
		case 30:
			temp |= TRANS_MSA_10_BPC;
			break;
		case 36:
			temp |= TRANS_MSA_12_BPC;
			break;
		default:
			BUG();
		}
		I915_WRITE(TRANS_MSA_MISC(cpu_transcoder), temp);
	}
}

void intel_ddi_set_vc_payload_alloc(struct drm_crtc *crtc, bool state)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	uint32_t temp;
	temp = I915_READ(TRANS_DDI_FUNC_CTL(cpu_transcoder));
	if (state == true)
		temp |= TRANS_DDI_DP_VC_PAYLOAD_ALLOC;
	else
		temp &= ~TRANS_DDI_DP_VC_PAYLOAD_ALLOC;
	I915_WRITE(TRANS_DDI_FUNC_CTL(cpu_transcoder), temp);
}

void intel_ddi_enable_transcoder_func(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *intel_encoder = intel_ddi_get_crtc_encoder(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = intel_crtc->pipe;
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	int type = intel_encoder->type;
	uint32_t temp;

	/* Enable TRANS_DDI_FUNC_CTL for the pipe to work in HDMI mode */
	temp = TRANS_DDI_FUNC_ENABLE;
	temp |= TRANS_DDI_SELECT_PORT(port);

	switch (intel_crtc->config->pipe_bpp) {
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

	if (intel_crtc->config->base.adjusted_mode.flags & DRM_MODE_FLAG_PVSYNC)
		temp |= TRANS_DDI_PVSYNC;
	if (intel_crtc->config->base.adjusted_mode.flags & DRM_MODE_FLAG_PHSYNC)
		temp |= TRANS_DDI_PHSYNC;

	if (cpu_transcoder == TRANSCODER_EDP) {
		switch (pipe) {
		case PIPE_A:
			/* On Haswell, can only use the always-on power well for
			 * eDP when not using the panel fitter, and when not
			 * using motion blur mitigation (which we don't
			 * support). */
			if (IS_HASWELL(dev) &&
			    (intel_crtc->config->pch_pfit.enabled ||
			     intel_crtc->config->pch_pfit.force_thru))
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

	if (type == INTEL_OUTPUT_HDMI) {
		if (intel_crtc->config->has_hdmi_sink)
			temp |= TRANS_DDI_MODE_SELECT_HDMI;
		else
			temp |= TRANS_DDI_MODE_SELECT_DVI;
	} else if (type == INTEL_OUTPUT_ANALOG) {
		temp |= TRANS_DDI_MODE_SELECT_FDI;
		temp |= (intel_crtc->config->fdi_lanes - 1) << 1;
	} else if (type == INTEL_OUTPUT_DP ||
		   type == INTEL_OUTPUT_EDP) {
		temp |= TRANS_DDI_MODE_SELECT_DP_SST;
		temp |= DDI_PORT_WIDTH(intel_crtc->config->lane_count);
	} else if (type == INTEL_OUTPUT_DP_MST) {
		temp |= TRANS_DDI_MODE_SELECT_DP_MST;
		temp |= DDI_PORT_WIDTH(intel_crtc->config->lane_count);
	} else {
		WARN(1, "Invalid encoder type %d for pipe %c\n",
		     intel_encoder->type, pipe_name(pipe));
	}

	I915_WRITE(TRANS_DDI_FUNC_CTL(cpu_transcoder), temp);
}

void intel_ddi_disable_transcoder_func(struct drm_i915_private *dev_priv,
				       enum transcoder cpu_transcoder)
{
	i915_reg_t reg = TRANS_DDI_FUNC_CTL(cpu_transcoder);
	uint32_t val = I915_READ(reg);

	val &= ~(TRANS_DDI_FUNC_ENABLE | TRANS_DDI_PORT_MASK | TRANS_DDI_DP_VC_PAYLOAD_ALLOC);
	val |= TRANS_DDI_PORT_NONE;
	I915_WRITE(reg, val);
}

bool intel_ddi_connector_get_hw_state(struct intel_connector *intel_connector)
{
	struct drm_device *dev = intel_connector->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_encoder *intel_encoder = intel_connector->encoder;
	int type = intel_connector->base.connector_type;
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	enum pipe pipe = 0;
	enum transcoder cpu_transcoder;
	enum intel_display_power_domain power_domain;
	uint32_t tmp;
	bool ret;

	power_domain = intel_display_port_power_domain(intel_encoder);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;

	if (!intel_encoder->get_hw_state(intel_encoder, &pipe)) {
		ret = false;
		goto out;
	}

	if (port == PORT_A)
		cpu_transcoder = TRANSCODER_EDP;
	else
		cpu_transcoder = (enum transcoder) pipe;

	tmp = I915_READ(TRANS_DDI_FUNC_CTL(cpu_transcoder));

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
	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

bool intel_ddi_get_hw_state(struct intel_encoder *encoder,
			    enum pipe *pipe)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = intel_ddi_get_encoder_port(encoder);
	enum intel_display_power_domain power_domain;
	u32 tmp;
	int i;
	bool ret;

	power_domain = intel_display_port_power_domain(encoder);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;

	ret = false;

	tmp = I915_READ(DDI_BUF_CTL(port));

	if (!(tmp & DDI_BUF_CTL_ENABLE))
		goto out;

	if (port == PORT_A) {
		tmp = I915_READ(TRANS_DDI_FUNC_CTL(TRANSCODER_EDP));

		switch (tmp & TRANS_DDI_EDP_INPUT_MASK) {
		case TRANS_DDI_EDP_INPUT_A_ON:
		case TRANS_DDI_EDP_INPUT_A_ONOFF:
			*pipe = PIPE_A;
			break;
		case TRANS_DDI_EDP_INPUT_B_ONOFF:
			*pipe = PIPE_B;
			break;
		case TRANS_DDI_EDP_INPUT_C_ONOFF:
			*pipe = PIPE_C;
			break;
		}

		ret = true;

		goto out;
	}

	for (i = TRANSCODER_A; i <= TRANSCODER_C; i++) {
		tmp = I915_READ(TRANS_DDI_FUNC_CTL(i));

		if ((tmp & TRANS_DDI_PORT_MASK) == TRANS_DDI_SELECT_PORT(port)) {
			if ((tmp & TRANS_DDI_MODE_SELECT_MASK) ==
			    TRANS_DDI_MODE_SELECT_DP_MST)
				goto out;

			*pipe = i;
			ret = true;

			goto out;
		}
	}

	DRM_DEBUG_KMS("No pipe for ddi port %c found\n", port_name(port));

out:
	if (ret && IS_BROXTON(dev_priv)) {
		tmp = I915_READ(BXT_PHY_CTL(port));
		if ((tmp & (BXT_PHY_LANE_POWERDOWN_ACK |
			    BXT_PHY_LANE_ENABLED)) != BXT_PHY_LANE_ENABLED)
			DRM_ERROR("Port %c enabled but PHY powered down? "
				  "(PHY_CTL %08x)\n", port_name(port), tmp);
	}

	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

void intel_ddi_enable_pipe_clock(struct intel_crtc *intel_crtc)
{
	struct drm_crtc *crtc = &intel_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_encoder *intel_encoder = intel_ddi_get_crtc_encoder(crtc);
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;

	if (cpu_transcoder != TRANSCODER_EDP)
		I915_WRITE(TRANS_CLK_SEL(cpu_transcoder),
			   TRANS_CLK_SEL_PORT(port));
}

void intel_ddi_disable_pipe_clock(struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;

	if (cpu_transcoder != TRANSCODER_EDP)
		I915_WRITE(TRANS_CLK_SEL(cpu_transcoder),
			   TRANS_CLK_SEL_DISABLED);
}

static void _skl_ddi_set_iboost(struct drm_i915_private *dev_priv,
				enum port port, uint8_t iboost)
{
	u32 tmp;

	tmp = I915_READ(DISPIO_CR_TX_BMU_CR0);
	tmp &= ~(BALANCE_LEG_MASK(port) | BALANCE_LEG_DISABLE(port));
	if (iboost)
		tmp |= iboost << BALANCE_LEG_SHIFT(port);
	else
		tmp |= BALANCE_LEG_DISABLE(port);
	I915_WRITE(DISPIO_CR_TX_BMU_CR0, tmp);
}

static void skl_ddi_set_iboost(struct intel_encoder *encoder, u32 level)
{
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(intel_dig_port->base.base.dev);
	enum port port = intel_dig_port->port;
	int type = encoder->type;
	const struct ddi_buf_trans *ddi_translations;
	uint8_t iboost;
	uint8_t dp_iboost, hdmi_iboost;
	int n_entries;

	/* VBT may override standard boost values */
	dp_iboost = dev_priv->vbt.ddi_port_info[port].dp_boost_level;
	hdmi_iboost = dev_priv->vbt.ddi_port_info[port].hdmi_boost_level;

	if (type == INTEL_OUTPUT_DP) {
		if (dp_iboost) {
			iboost = dp_iboost;
		} else {
			ddi_translations = skl_get_buf_trans_dp(dev_priv, &n_entries);
			iboost = ddi_translations[level].i_boost;
		}
	} else if (type == INTEL_OUTPUT_EDP) {
		if (dp_iboost) {
			iboost = dp_iboost;
		} else {
			ddi_translations = skl_get_buf_trans_edp(dev_priv, &n_entries);

			if (WARN_ON(port != PORT_A &&
				    port != PORT_E && n_entries > 9))
				n_entries = 9;

			iboost = ddi_translations[level].i_boost;
		}
	} else if (type == INTEL_OUTPUT_HDMI) {
		if (hdmi_iboost) {
			iboost = hdmi_iboost;
		} else {
			ddi_translations = skl_get_buf_trans_hdmi(dev_priv, &n_entries);
			iboost = ddi_translations[level].i_boost;
		}
	} else {
		return;
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

static void bxt_ddi_vswing_sequence(struct drm_i915_private *dev_priv,
				    u32 level, enum port port, int type)
{
	const struct bxt_ddi_buf_trans *ddi_translations;
	u32 n_entries, i;
	uint32_t val;

	if (type == INTEL_OUTPUT_EDP && dev_priv->vbt.edp.low_vswing) {
		n_entries = ARRAY_SIZE(bxt_ddi_translations_edp);
		ddi_translations = bxt_ddi_translations_edp;
	} else if (type == INTEL_OUTPUT_DP
			|| type == INTEL_OUTPUT_EDP) {
		n_entries = ARRAY_SIZE(bxt_ddi_translations_dp);
		ddi_translations = bxt_ddi_translations_dp;
	} else if (type == INTEL_OUTPUT_HDMI) {
		n_entries = ARRAY_SIZE(bxt_ddi_translations_hdmi);
		ddi_translations = bxt_ddi_translations_hdmi;
	} else {
		DRM_DEBUG_KMS("Vswing programming not done for encoder %d\n",
				type);
		return;
	}

	/* Check if default value has to be used */
	if (level >= n_entries ||
	    (type == INTEL_OUTPUT_HDMI && level == HDMI_LEVEL_SHIFT_UNKNOWN)) {
		for (i = 0; i < n_entries; i++) {
			if (ddi_translations[i].default_index) {
				level = i;
				break;
			}
		}
	}

	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers and we pick lanes 0/1 for that.
	 */
	val = I915_READ(BXT_PORT_PCS_DW10_LN01(port));
	val &= ~(TX2_SWING_CALC_INIT | TX1_SWING_CALC_INIT);
	I915_WRITE(BXT_PORT_PCS_DW10_GRP(port), val);

	val = I915_READ(BXT_PORT_TX_DW2_LN0(port));
	val &= ~(MARGIN_000 | UNIQ_TRANS_SCALE);
	val |= ddi_translations[level].margin << MARGIN_000_SHIFT |
	       ddi_translations[level].scale << UNIQ_TRANS_SCALE_SHIFT;
	I915_WRITE(BXT_PORT_TX_DW2_GRP(port), val);

	val = I915_READ(BXT_PORT_TX_DW3_LN0(port));
	val &= ~SCALE_DCOMP_METHOD;
	if (ddi_translations[level].enable)
		val |= SCALE_DCOMP_METHOD;

	if ((val & UNIQUE_TRANGE_EN_METHOD) && !(val & SCALE_DCOMP_METHOD))
		DRM_ERROR("Disabled scaling while ouniqetrangenmethod was set");

	I915_WRITE(BXT_PORT_TX_DW3_GRP(port), val);

	val = I915_READ(BXT_PORT_TX_DW4_LN0(port));
	val &= ~DE_EMPHASIS;
	val |= ddi_translations[level].deemphasis << DEEMPH_SHIFT;
	I915_WRITE(BXT_PORT_TX_DW4_GRP(port), val);

	val = I915_READ(BXT_PORT_PCS_DW10_LN01(port));
	val |= TX2_SWING_CALC_INIT | TX1_SWING_CALC_INIT;
	I915_WRITE(BXT_PORT_PCS_DW10_GRP(port), val);
}

static uint32_t translate_signal_level(int signal_levels)
{
	uint32_t level;

	switch (signal_levels) {
	default:
		DRM_DEBUG_KMS("Unsupported voltage swing/pre-emphasis level: 0x%x\n",
			      signal_levels);
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		level = 0;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		level = 1;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_2:
		level = 2;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_3:
		level = 3;
		break;

	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		level = 4;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		level = 5;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_2:
		level = 6;
		break;

	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		level = 7;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		level = 8;
		break;

	case DP_TRAIN_VOLTAGE_SWING_LEVEL_3 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		level = 9;
		break;
	}

	return level;
}

uint32_t ddi_signal_levels(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dport->base.base.dev);
	struct intel_encoder *encoder = &dport->base;
	uint8_t train_set = intel_dp->train_set[0];
	int signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
					 DP_TRAIN_PRE_EMPHASIS_MASK);
	enum port port = dport->port;
	uint32_t level;

	level = translate_signal_level(signal_levels);

	if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv))
		skl_ddi_set_iboost(encoder, level);
	else if (IS_BROXTON(dev_priv))
		bxt_ddi_vswing_sequence(dev_priv, level, port, encoder->type);

	return DDI_BUF_TRANS_SELECT(level);
}

void intel_ddi_clk_select(struct intel_encoder *encoder,
			  struct intel_shared_dpll *pll)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = intel_ddi_get_encoder_port(encoder);

	if (WARN_ON(!pll))
		return;

	if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv)) {
		uint32_t val;

		/* DDI -> PLL mapping  */
		val = I915_READ(DPLL_CTRL2);

		val &= ~(DPLL_CTRL2_DDI_CLK_OFF(port) |
			DPLL_CTRL2_DDI_CLK_SEL_MASK(port));
		val |= (DPLL_CTRL2_DDI_CLK_SEL(pll->id, port) |
			DPLL_CTRL2_DDI_SEL_OVERRIDE(port));

		I915_WRITE(DPLL_CTRL2, val);

	} else if (INTEL_INFO(dev_priv)->gen < 9) {
		I915_WRITE(PORT_CLK_SEL(port), hsw_pll_to_ddi_pll_sel(pll));
	}
}

static void intel_ddi_pre_enable_dp(struct intel_encoder *encoder,
				    int link_rate, uint32_t lane_count,
				    struct intel_shared_dpll *pll,
				    bool link_mst)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = intel_ddi_get_encoder_port(encoder);

	intel_dp_set_link_params(intel_dp, link_rate, lane_count,
				 link_mst);
	if (encoder->type == INTEL_OUTPUT_EDP)
		intel_edp_panel_on(intel_dp);

	intel_ddi_clk_select(encoder, pll);
	intel_prepare_dp_ddi_buffers(encoder);
	intel_ddi_init_dp_buf_reg(encoder);
	intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_ON);
	intel_dp_start_link_train(intel_dp);
	if (port != PORT_A || INTEL_GEN(dev_priv) >= 9)
		intel_dp_stop_link_train(intel_dp);
}

static void intel_ddi_pre_enable_hdmi(struct intel_encoder *encoder,
				      bool has_hdmi_sink,
				      struct drm_display_mode *adjusted_mode,
				      struct intel_shared_dpll *pll)
{
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_encoder *drm_encoder = &encoder->base;
	enum port port = intel_ddi_get_encoder_port(encoder);
	int level = intel_ddi_hdmi_level(dev_priv, port);

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, true);
	intel_ddi_clk_select(encoder, pll);
	intel_prepare_hdmi_ddi_buffers(encoder);
	if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv))
		skl_ddi_set_iboost(encoder, level);
	else if (IS_BROXTON(dev_priv))
		bxt_ddi_vswing_sequence(dev_priv, level, port,
					INTEL_OUTPUT_HDMI);

	intel_hdmi->set_infoframes(drm_encoder,
				   has_hdmi_sink,
				   adjusted_mode);
}

static void intel_ddi_pre_enable(struct intel_encoder *intel_encoder,
				 struct intel_crtc_state *pipe_config,
				 struct drm_connector_state *conn_state)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct intel_crtc *crtc = to_intel_crtc(encoder->crtc);
	int type = intel_encoder->type;

	if (type == INTEL_OUTPUT_DP || type == INTEL_OUTPUT_EDP) {
		intel_ddi_pre_enable_dp(intel_encoder,
					crtc->config->port_clock,
					crtc->config->lane_count,
					crtc->config->shared_dpll,
					intel_crtc_has_type(crtc->config,
							    INTEL_OUTPUT_DP_MST));
	}
	if (type == INTEL_OUTPUT_HDMI) {
		intel_ddi_pre_enable_hdmi(intel_encoder,
					  crtc->config->has_hdmi_sink,
					  &crtc->config->base.adjusted_mode,
					  crtc->config->shared_dpll);
	}
}

static void intel_ddi_post_disable(struct intel_encoder *intel_encoder,
				   struct intel_crtc_state *old_crtc_state,
				   struct drm_connector_state *old_conn_state)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	int type = intel_encoder->type;
	uint32_t val;
	bool wait = false;

	/* old_crtc_state and old_conn_state are NULL when called from DP_MST */

	val = I915_READ(DDI_BUF_CTL(port));
	if (val & DDI_BUF_CTL_ENABLE) {
		val &= ~DDI_BUF_CTL_ENABLE;
		I915_WRITE(DDI_BUF_CTL(port), val);
		wait = true;
	}

	val = I915_READ(DP_TP_CTL(port));
	val &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
	val |= DP_TP_CTL_LINK_TRAIN_PAT1;
	I915_WRITE(DP_TP_CTL(port), val);

	if (wait)
		intel_wait_ddi_buf_idle(dev_priv, port);

	if (type == INTEL_OUTPUT_DP || type == INTEL_OUTPUT_EDP) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
		intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_OFF);
		intel_edp_panel_vdd_on(intel_dp);
		intel_edp_panel_off(intel_dp);
	}

	if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev))
		I915_WRITE(DPLL_CTRL2, (I915_READ(DPLL_CTRL2) |
					DPLL_CTRL2_DDI_CLK_OFF(port)));
	else if (INTEL_INFO(dev)->gen < 9)
		I915_WRITE(PORT_CLK_SEL(port), PORT_CLK_SEL_NONE);

	if (type == INTEL_OUTPUT_HDMI) {
		struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);

		intel_dp_dual_mode_set_tmds_output(intel_hdmi, false);
	}
}

void intel_ddi_fdi_post_disable(struct intel_encoder *intel_encoder,
				struct intel_crtc_state *old_crtc_state,
				struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(intel_encoder->base.dev);
	uint32_t val;

	/*
	 * Bspec lists this as both step 13 (before DDI_BUF_CTL disable)
	 * and step 18 (after clearing PORT_CLK_SEL). Based on a BUN,
	 * step 13 is the correct place for it. Step 18 is where it was
	 * originally before the BUN.
	 */
	val = I915_READ(FDI_RX_CTL(PIPE_A));
	val &= ~FDI_RX_ENABLE;
	I915_WRITE(FDI_RX_CTL(PIPE_A), val);

	intel_ddi_post_disable(intel_encoder, old_crtc_state, old_conn_state);

	val = I915_READ(FDI_RX_MISC(PIPE_A));
	val &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
	val |= FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2);
	I915_WRITE(FDI_RX_MISC(PIPE_A), val);

	val = I915_READ(FDI_RX_CTL(PIPE_A));
	val &= ~FDI_PCDCLK;
	I915_WRITE(FDI_RX_CTL(PIPE_A), val);

	val = I915_READ(FDI_RX_CTL(PIPE_A));
	val &= ~FDI_RX_PLL_ENABLE;
	I915_WRITE(FDI_RX_CTL(PIPE_A), val);
}

static void intel_enable_ddi(struct intel_encoder *intel_encoder,
			     struct intel_crtc_state *pipe_config,
			     struct drm_connector_state *conn_state)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_crtc *crtc = encoder->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	int type = intel_encoder->type;

	if (type == INTEL_OUTPUT_HDMI) {
		struct intel_digital_port *intel_dig_port =
			enc_to_dig_port(encoder);

		/* In HDMI/DVI mode, the port width, and swing/emphasis values
		 * are ignored so nothing special needs to be done besides
		 * enabling the port.
		 */
		I915_WRITE(DDI_BUF_CTL(port),
			   intel_dig_port->saved_port_bits |
			   DDI_BUF_CTL_ENABLE);
	} else if (type == INTEL_OUTPUT_EDP) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		if (port == PORT_A && INTEL_INFO(dev)->gen < 9)
			intel_dp_stop_link_train(intel_dp);

		intel_edp_backlight_on(intel_dp);
		intel_psr_enable(intel_dp);
		intel_edp_drrs_enable(intel_dp, pipe_config);
	}

	if (intel_crtc->config->has_audio) {
		intel_display_power_get(dev_priv, POWER_DOMAIN_AUDIO);
		intel_audio_codec_enable(intel_encoder);
	}
}

static void intel_disable_ddi(struct intel_encoder *intel_encoder,
			      struct intel_crtc_state *old_crtc_state,
			      struct drm_connector_state *old_conn_state)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_crtc *crtc = encoder->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int type = intel_encoder->type;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (intel_crtc->config->has_audio) {
		intel_audio_codec_disable(intel_encoder);
		intel_display_power_put(dev_priv, POWER_DOMAIN_AUDIO);
	}

	if (type == INTEL_OUTPUT_EDP) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		intel_edp_drrs_disable(intel_dp, old_crtc_state);
		intel_psr_disable(intel_dp);
		intel_edp_backlight_off(intel_dp);
	}
}

bool bxt_ddi_phy_is_enabled(struct drm_i915_private *dev_priv,
			    enum dpio_phy phy)
{
	enum port port;

	if (!(I915_READ(BXT_P_CR_GT_DISP_PWRON) & GT_DISPLAY_POWER_ON(phy)))
		return false;

	if ((I915_READ(BXT_PORT_CL1CM_DW0(phy)) &
	     (PHY_POWER_GOOD | PHY_RESERVED)) != PHY_POWER_GOOD) {
		DRM_DEBUG_DRIVER("DDI PHY %d powered, but power hasn't settled\n",
				 phy);

		return false;
	}

	if (phy == DPIO_PHY1 &&
	    !(I915_READ(BXT_PORT_REF_DW3(DPIO_PHY1)) & GRC_DONE)) {
		DRM_DEBUG_DRIVER("DDI PHY 1 powered, but GRC isn't done\n");

		return false;
	}

	if (!(I915_READ(BXT_PHY_CTL_FAMILY(phy)) & COMMON_RESET_DIS)) {
		DRM_DEBUG_DRIVER("DDI PHY %d powered, but still in reset\n",
				 phy);

		return false;
	}

	for_each_port_masked(port,
			     phy == DPIO_PHY0 ? BIT(PORT_B) | BIT(PORT_C) :
						BIT(PORT_A)) {
		u32 tmp = I915_READ(BXT_PHY_CTL(port));

		if (tmp & BXT_PHY_CMNLANE_POWERDOWN_ACK) {
			DRM_DEBUG_DRIVER("DDI PHY %d powered, but common lane "
					 "for port %c powered down "
					 "(PHY_CTL %08x)\n",
					 phy, port_name(port), tmp);

			return false;
		}
	}

	return true;
}

static u32 bxt_get_grc(struct drm_i915_private *dev_priv, enum dpio_phy phy)
{
	u32 val = I915_READ(BXT_PORT_REF_DW6(phy));

	return (val & GRC_CODE_MASK) >> GRC_CODE_SHIFT;
}

static void bxt_phy_wait_grc_done(struct drm_i915_private *dev_priv,
				  enum dpio_phy phy)
{
	if (intel_wait_for_register(dev_priv,
				    BXT_PORT_REF_DW3(phy),
				    GRC_DONE, GRC_DONE,
				    10))
		DRM_ERROR("timeout waiting for PHY%d GRC\n", phy);
}

void bxt_ddi_phy_init(struct drm_i915_private *dev_priv, enum dpio_phy phy)
{
	u32 val;

	if (bxt_ddi_phy_is_enabled(dev_priv, phy)) {
		/* Still read out the GRC value for state verification */
		if (phy == DPIO_PHY0)
			dev_priv->bxt_phy_grc = bxt_get_grc(dev_priv, phy);

		if (bxt_ddi_phy_verify_state(dev_priv, phy)) {
			DRM_DEBUG_DRIVER("DDI PHY %d already enabled, "
					 "won't reprogram it\n", phy);

			return;
		}

		DRM_DEBUG_DRIVER("DDI PHY %d enabled with invalid state, "
				 "force reprogramming it\n", phy);
	}

	val = I915_READ(BXT_P_CR_GT_DISP_PWRON);
	val |= GT_DISPLAY_POWER_ON(phy);
	I915_WRITE(BXT_P_CR_GT_DISP_PWRON, val);

	/*
	 * The PHY registers start out inaccessible and respond to reads with
	 * all 1s.  Eventually they become accessible as they power up, then
	 * the reserved bit will give the default 0.  Poll on the reserved bit
	 * becoming 0 to find when the PHY is accessible.
	 * HW team confirmed that the time to reach phypowergood status is
	 * anywhere between 50 us and 100us.
	 */
	if (wait_for_us(((I915_READ(BXT_PORT_CL1CM_DW0(phy)) &
		(PHY_RESERVED | PHY_POWER_GOOD)) == PHY_POWER_GOOD), 100)) {
		DRM_ERROR("timeout during PHY%d power on\n", phy);
	}

	/* Program PLL Rcomp code offset */
	val = I915_READ(BXT_PORT_CL1CM_DW9(phy));
	val &= ~IREF0RC_OFFSET_MASK;
	val |= 0xE4 << IREF0RC_OFFSET_SHIFT;
	I915_WRITE(BXT_PORT_CL1CM_DW9(phy), val);

	val = I915_READ(BXT_PORT_CL1CM_DW10(phy));
	val &= ~IREF1RC_OFFSET_MASK;
	val |= 0xE4 << IREF1RC_OFFSET_SHIFT;
	I915_WRITE(BXT_PORT_CL1CM_DW10(phy), val);

	/* Program power gating */
	val = I915_READ(BXT_PORT_CL1CM_DW28(phy));
	val |= OCL1_POWER_DOWN_EN | DW28_OLDO_DYN_PWR_DOWN_EN |
		SUS_CLK_CONFIG;
	I915_WRITE(BXT_PORT_CL1CM_DW28(phy), val);

	if (phy == DPIO_PHY0) {
		val = I915_READ(BXT_PORT_CL2CM_DW6_BC);
		val |= DW6_OLDO_DYN_PWR_DOWN_EN;
		I915_WRITE(BXT_PORT_CL2CM_DW6_BC, val);
	}

	val = I915_READ(BXT_PORT_CL1CM_DW30(phy));
	val &= ~OCL2_LDOFUSE_PWR_DIS;
	/*
	 * On PHY1 disable power on the second channel, since no port is
	 * connected there. On PHY0 both channels have a port, so leave it
	 * enabled.
	 * TODO: port C is only connected on BXT-P, so on BXT0/1 we should
	 * power down the second channel on PHY0 as well.
	 *
	 * FIXME: Clarify programming of the following, the register is
	 * read-only with bit 6 fixed at 0 at least in stepping A.
	 */
	if (phy == DPIO_PHY1)
		val |= OCL2_LDOFUSE_PWR_DIS;
	I915_WRITE(BXT_PORT_CL1CM_DW30(phy), val);

	if (phy == DPIO_PHY0) {
		uint32_t grc_code;
		/*
		 * PHY0 isn't connected to an RCOMP resistor so copy over
		 * the corresponding calibrated value from PHY1, and disable
		 * the automatic calibration on PHY0.
		 */
		val = dev_priv->bxt_phy_grc = bxt_get_grc(dev_priv, DPIO_PHY1);
		grc_code = val << GRC_CODE_FAST_SHIFT |
			   val << GRC_CODE_SLOW_SHIFT |
			   val;
		I915_WRITE(BXT_PORT_REF_DW6(DPIO_PHY0), grc_code);

		val = I915_READ(BXT_PORT_REF_DW8(DPIO_PHY0));
		val |= GRC_DIS | GRC_RDY_OVRD;
		I915_WRITE(BXT_PORT_REF_DW8(DPIO_PHY0), val);
	}

	val = I915_READ(BXT_PHY_CTL_FAMILY(phy));
	val |= COMMON_RESET_DIS;
	I915_WRITE(BXT_PHY_CTL_FAMILY(phy), val);

	if (phy == DPIO_PHY1)
		bxt_phy_wait_grc_done(dev_priv, DPIO_PHY1);
}

void bxt_ddi_phy_uninit(struct drm_i915_private *dev_priv, enum dpio_phy phy)
{
	uint32_t val;

	val = I915_READ(BXT_PHY_CTL_FAMILY(phy));
	val &= ~COMMON_RESET_DIS;
	I915_WRITE(BXT_PHY_CTL_FAMILY(phy), val);

	val = I915_READ(BXT_P_CR_GT_DISP_PWRON);
	val &= ~GT_DISPLAY_POWER_ON(phy);
	I915_WRITE(BXT_P_CR_GT_DISP_PWRON, val);
}

static bool __printf(6, 7)
__phy_reg_verify_state(struct drm_i915_private *dev_priv, enum dpio_phy phy,
		       i915_reg_t reg, u32 mask, u32 expected,
		       const char *reg_fmt, ...)
{
	struct va_format vaf;
	va_list args;
	u32 val;

	val = I915_READ(reg);
	if ((val & mask) == expected)
		return true;

	va_start(args, reg_fmt);
	vaf.fmt = reg_fmt;
	vaf.va = &args;

	DRM_DEBUG_DRIVER("DDI PHY %d reg %pV [%08x] state mismatch: "
			 "current %08x, expected %08x (mask %08x)\n",
			 phy, &vaf, reg.reg, val, (val & ~mask) | expected,
			 mask);

	va_end(args);

	return false;
}

bool bxt_ddi_phy_verify_state(struct drm_i915_private *dev_priv,
			      enum dpio_phy phy)
{
	uint32_t mask;
	bool ok;

#define _CHK(reg, mask, exp, fmt, ...)					\
	__phy_reg_verify_state(dev_priv, phy, reg, mask, exp, fmt,	\
			       ## __VA_ARGS__)

	if (!bxt_ddi_phy_is_enabled(dev_priv, phy))
		return false;

	ok = true;

	/* PLL Rcomp code offset */
	ok &= _CHK(BXT_PORT_CL1CM_DW9(phy),
		    IREF0RC_OFFSET_MASK, 0xe4 << IREF0RC_OFFSET_SHIFT,
		    "BXT_PORT_CL1CM_DW9(%d)", phy);
	ok &= _CHK(BXT_PORT_CL1CM_DW10(phy),
		    IREF1RC_OFFSET_MASK, 0xe4 << IREF1RC_OFFSET_SHIFT,
		    "BXT_PORT_CL1CM_DW10(%d)", phy);

	/* Power gating */
	mask = OCL1_POWER_DOWN_EN | DW28_OLDO_DYN_PWR_DOWN_EN | SUS_CLK_CONFIG;
	ok &= _CHK(BXT_PORT_CL1CM_DW28(phy), mask, mask,
		    "BXT_PORT_CL1CM_DW28(%d)", phy);

	if (phy == DPIO_PHY0)
		ok &= _CHK(BXT_PORT_CL2CM_DW6_BC,
			   DW6_OLDO_DYN_PWR_DOWN_EN, DW6_OLDO_DYN_PWR_DOWN_EN,
			   "BXT_PORT_CL2CM_DW6_BC");

	/*
	 * TODO: Verify BXT_PORT_CL1CM_DW30 bit OCL2_LDOFUSE_PWR_DIS,
	 * at least on stepping A this bit is read-only and fixed at 0.
	 */

	if (phy == DPIO_PHY0) {
		u32 grc_code = dev_priv->bxt_phy_grc;

		grc_code = grc_code << GRC_CODE_FAST_SHIFT |
			   grc_code << GRC_CODE_SLOW_SHIFT |
			   grc_code;
		mask = GRC_CODE_FAST_MASK | GRC_CODE_SLOW_MASK |
		       GRC_CODE_NOM_MASK;
		ok &= _CHK(BXT_PORT_REF_DW6(DPIO_PHY0), mask, grc_code,
			    "BXT_PORT_REF_DW6(%d)", DPIO_PHY0);

		mask = GRC_DIS | GRC_RDY_OVRD;
		ok &= _CHK(BXT_PORT_REF_DW8(DPIO_PHY0), mask, mask,
			    "BXT_PORT_REF_DW8(%d)", DPIO_PHY0);
	}

	return ok;
#undef _CHK
}

static uint8_t
bxt_ddi_phy_calc_lane_lat_optim_mask(struct intel_encoder *encoder,
				     struct intel_crtc_state *pipe_config)
{
	switch (pipe_config->lane_count) {
	case 1:
		return 0;
	case 2:
		return BIT(2) | BIT(0);
	case 4:
		return BIT(3) | BIT(2) | BIT(0);
	default:
		MISSING_CASE(pipe_config->lane_count);

		return 0;
	}
}

static void bxt_ddi_pre_pll_enable(struct intel_encoder *encoder,
				   struct intel_crtc_state *pipe_config,
				   struct drm_connector_state *conn_state)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(dport->base.base.dev);
	enum port port = dport->port;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	int lane;

	for (lane = 0; lane < 4; lane++) {
		u32 val = I915_READ(BXT_PORT_TX_DW14_LN(port, lane));

		/*
		 * Note that on CHV this flag is called UPAR, but has
		 * the same function.
		 */
		val &= ~LATENCY_OPTIM;
		if (intel_crtc->config->lane_lat_optim_mask & BIT(lane))
			val |= LATENCY_OPTIM;

		I915_WRITE(BXT_PORT_TX_DW14_LN(port, lane), val);
	}
}

static uint8_t
bxt_ddi_phy_get_lane_lat_optim_mask(struct intel_encoder *encoder)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(dport->base.base.dev);
	enum port port = dport->port;
	int lane;
	uint8_t mask;

	mask = 0;
	for (lane = 0; lane < 4; lane++) {
		u32 val = I915_READ(BXT_PORT_TX_DW14_LN(port, lane));

		if (val & LATENCY_OPTIM)
			mask |= BIT(lane);
	}

	return mask;
}

void intel_ddi_prepare_link_retrain(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv =
		to_i915(intel_dig_port->base.base.dev);
	enum port port = intel_dig_port->port;
	uint32_t val;
	bool wait = false;

	if (I915_READ(DP_TP_CTL(port)) & DP_TP_CTL_ENABLE) {
		val = I915_READ(DDI_BUF_CTL(port));
		if (val & DDI_BUF_CTL_ENABLE) {
			val &= ~DDI_BUF_CTL_ENABLE;
			I915_WRITE(DDI_BUF_CTL(port), val);
			wait = true;
		}

		val = I915_READ(DP_TP_CTL(port));
		val &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
		val |= DP_TP_CTL_LINK_TRAIN_PAT1;
		I915_WRITE(DP_TP_CTL(port), val);
		POSTING_READ(DP_TP_CTL(port));

		if (wait)
			intel_wait_ddi_buf_idle(dev_priv, port);
	}

	val = DP_TP_CTL_ENABLE |
	      DP_TP_CTL_LINK_TRAIN_PAT1 | DP_TP_CTL_SCRAMBLE_DISABLE;
	if (intel_dp->link_mst)
		val |= DP_TP_CTL_MODE_MST;
	else {
		val |= DP_TP_CTL_MODE_SST;
		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			val |= DP_TP_CTL_ENHANCED_FRAME_ENABLE;
	}
	I915_WRITE(DP_TP_CTL(port), val);
	POSTING_READ(DP_TP_CTL(port));

	intel_dp->DP |= DDI_BUF_CTL_ENABLE;
	I915_WRITE(DDI_BUF_CTL(port), intel_dp->DP);
	POSTING_READ(DDI_BUF_CTL(port));

	udelay(600);
}

void intel_ddi_get_config(struct intel_encoder *encoder,
			  struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	enum transcoder cpu_transcoder = pipe_config->cpu_transcoder;
	struct intel_hdmi *intel_hdmi;
	u32 temp, flags = 0;

	/* XXX: DSI transcoder paranoia */
	if (WARN_ON(transcoder_is_dsi(cpu_transcoder)))
		return;

	temp = I915_READ(TRANS_DDI_FUNC_CTL(cpu_transcoder));
	if (temp & TRANS_DDI_PHSYNC)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;
	if (temp & TRANS_DDI_PVSYNC)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;

	pipe_config->base.adjusted_mode.flags |= flags;

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
		intel_hdmi = enc_to_intel_hdmi(&encoder->base);

		if (intel_hdmi->infoframe_enabled(&encoder->base, pipe_config))
			pipe_config->has_infoframe = true;
		/* fall through */
	case TRANS_DDI_MODE_SELECT_DVI:
		pipe_config->lane_count = 4;
		break;
	case TRANS_DDI_MODE_SELECT_FDI:
		break;
	case TRANS_DDI_MODE_SELECT_DP_SST:
	case TRANS_DDI_MODE_SELECT_DP_MST:
		pipe_config->lane_count =
			((temp & DDI_PORT_WIDTH_MASK) >> DDI_PORT_WIDTH_SHIFT) + 1;
		intel_dp_get_m_n(intel_crtc, pipe_config);
		break;
	default:
		break;
	}

	if (intel_display_power_is_enabled(dev_priv, POWER_DOMAIN_AUDIO)) {
		temp = I915_READ(HSW_AUD_PIN_ELD_CP_VLD);
		if (temp & AUDIO_OUTPUT_ENABLE(intel_crtc->pipe))
			pipe_config->has_audio = true;
	}

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

	if (IS_BROXTON(dev_priv))
		pipe_config->lane_lat_optim_mask =
			bxt_ddi_phy_get_lane_lat_optim_mask(encoder);
}

static bool intel_ddi_compute_config(struct intel_encoder *encoder,
				     struct intel_crtc_state *pipe_config,
				     struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int type = encoder->type;
	int port = intel_ddi_get_encoder_port(encoder);
	int ret;

	WARN(type == INTEL_OUTPUT_UNKNOWN, "compute_config() on unknown output!\n");

	if (port == PORT_A)
		pipe_config->cpu_transcoder = TRANSCODER_EDP;

	if (type == INTEL_OUTPUT_HDMI)
		ret = intel_hdmi_compute_config(encoder, pipe_config, conn_state);
	else
		ret = intel_dp_compute_config(encoder, pipe_config, conn_state);

	if (IS_BROXTON(dev_priv) && ret)
		pipe_config->lane_lat_optim_mask =
			bxt_ddi_phy_calc_lane_lat_optim_mask(encoder,
							     pipe_config);

	return ret;

}

static const struct drm_encoder_funcs intel_ddi_funcs = {
	.reset = intel_dp_encoder_reset,
	.destroy = intel_dp_encoder_destroy,
};

static struct intel_connector *
intel_ddi_init_dp_connector(struct intel_digital_port *intel_dig_port)
{
	struct intel_connector *connector;
	enum port port = intel_dig_port->port;

	connector = intel_connector_alloc();
	if (!connector)
		return NULL;

	intel_dig_port->dp.output_reg = DDI_BUF_CTL(port);
	if (!intel_dp_init_connector(intel_dig_port, connector)) {
		kfree(connector);
		return NULL;
	}

	return connector;
}

static struct intel_connector *
intel_ddi_init_hdmi_connector(struct intel_digital_port *intel_dig_port)
{
	struct intel_connector *connector;
	enum port port = intel_dig_port->port;

	connector = intel_connector_alloc();
	if (!connector)
		return NULL;

	intel_dig_port->hdmi.hdmi_reg = DDI_BUF_CTL(port);
	intel_hdmi_init_connector(intel_dig_port, connector);

	return connector;
}

struct intel_shared_dpll *
intel_ddi_get_link_dpll(struct intel_dp *intel_dp, int clock)
{
	struct intel_connector *connector = intel_dp->attached_connector;
	struct intel_encoder *encoder = connector->encoder;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_shared_dpll *pll = NULL;
	struct intel_shared_dpll_config tmp_pll_config;
	enum intel_dpll_id dpll_id;

	if (IS_BROXTON(dev_priv)) {
		dpll_id =  (enum intel_dpll_id)dig_port->port;
		/*
		 * Select the required PLL. This works for platforms where
		 * there is no shared DPLL.
		 */
		pll = &dev_priv->shared_dplls[dpll_id];
		if (WARN_ON(pll->active_mask)) {

			DRM_ERROR("Shared DPLL in use. active_mask:%x\n",
				  pll->active_mask);
			return NULL;
		}
		tmp_pll_config = pll->config;
		if (!bxt_ddi_dp_set_dpll_hw_state(clock,
						  &pll->config.hw_state)) {
			DRM_ERROR("Could not setup DPLL\n");
			pll->config = tmp_pll_config;
			return NULL;
		}
	} else if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv)) {
		pll = skl_find_link_pll(dev_priv, clock);
	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		pll = hsw_ddi_dp_get_dpll(encoder, clock);
	}
	return pll;
}

void intel_ddi_init(struct drm_device *dev, enum port port)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_digital_port *intel_dig_port;
	struct intel_encoder *intel_encoder;
	struct drm_encoder *encoder;
	bool init_hdmi, init_dp;
	int max_lanes;

	if (I915_READ(DDI_BUF_CTL(PORT_A)) & DDI_A_4_LANES) {
		switch (port) {
		case PORT_A:
			max_lanes = 4;
			break;
		case PORT_E:
			max_lanes = 0;
			break;
		default:
			max_lanes = 4;
			break;
		}
	} else {
		switch (port) {
		case PORT_A:
			max_lanes = 2;
			break;
		case PORT_E:
			max_lanes = 2;
			break;
		default:
			max_lanes = 4;
			break;
		}
	}

	init_hdmi = (dev_priv->vbt.ddi_port_info[port].supports_dvi ||
		     dev_priv->vbt.ddi_port_info[port].supports_hdmi);
	init_dp = dev_priv->vbt.ddi_port_info[port].supports_dp;
	if (!init_dp && !init_hdmi) {
		DRM_DEBUG_KMS("VBT says port %c is not DVI/HDMI/DP compatible, respect it\n",
			      port_name(port));
		return;
	}

	intel_dig_port = kzalloc(sizeof(*intel_dig_port), GFP_KERNEL);
	if (!intel_dig_port)
		return;

	intel_encoder = &intel_dig_port->base;
	encoder = &intel_encoder->base;

	drm_encoder_init(dev, encoder, &intel_ddi_funcs,
			 DRM_MODE_ENCODER_TMDS, "DDI %c", port_name(port));

	intel_encoder->compute_config = intel_ddi_compute_config;
	intel_encoder->enable = intel_enable_ddi;
	if (IS_BROXTON(dev_priv))
		intel_encoder->pre_pll_enable = bxt_ddi_pre_pll_enable;
	intel_encoder->pre_enable = intel_ddi_pre_enable;
	intel_encoder->disable = intel_disable_ddi;
	intel_encoder->post_disable = intel_ddi_post_disable;
	intel_encoder->get_hw_state = intel_ddi_get_hw_state;
	intel_encoder->get_config = intel_ddi_get_config;
	intel_encoder->suspend = intel_dp_encoder_suspend;

	intel_dig_port->port = port;
	intel_dig_port->saved_port_bits = I915_READ(DDI_BUF_CTL(port)) &
					  (DDI_BUF_PORT_REVERSAL |
					   DDI_A_4_LANES);

	/*
	 * Bspec says that DDI_A_4_LANES is the only supported configuration
	 * for Broxton.  Yet some BIOS fail to set this bit on port A if eDP
	 * wasn't lit up at boot.  Force this bit on in our internal
	 * configuration so that we use the proper lane count for our
	 * calculations.
	 */
	if (IS_BROXTON(dev) && port == PORT_A) {
		if (!(intel_dig_port->saved_port_bits & DDI_A_4_LANES)) {
			DRM_DEBUG_KMS("BXT BIOS forgot to set DDI_A_4_LANES for port A; fixing\n");
			intel_dig_port->saved_port_bits |= DDI_A_4_LANES;
			max_lanes = 4;
		}
	}

	intel_dig_port->max_lanes = max_lanes;

	intel_encoder->type = INTEL_OUTPUT_UNKNOWN;
	intel_encoder->port = port;
	intel_encoder->crtc_mask = (1 << 0) | (1 << 1) | (1 << 2);
	intel_encoder->cloneable = 0;

	if (init_dp) {
		if (!intel_ddi_init_dp_connector(intel_dig_port))
			goto err;

		intel_dig_port->hpd_pulse = intel_dp_hpd_pulse;
		/*
		 * On BXT A0/A1, sw needs to activate DDIA HPD logic and
		 * interrupts to check the external panel connection.
		 */
		if (IS_BXT_REVID(dev, 0, BXT_REVID_A1) && port == PORT_B)
			dev_priv->hotplug.irq_port[PORT_A] = intel_dig_port;
		else
			dev_priv->hotplug.irq_port[port] = intel_dig_port;
	}

	/* In theory we don't need the encoder->type check, but leave it just in
	 * case we have some really bad VBTs... */
	if (intel_encoder->type != INTEL_OUTPUT_EDP && init_hdmi) {
		if (!intel_ddi_init_hdmi_connector(intel_dig_port))
			goto err;
	}

	return;

err:
	drm_encoder_cleanup(encoder);
	kfree(intel_dig_port);
}

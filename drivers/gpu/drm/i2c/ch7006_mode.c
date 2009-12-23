/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "ch7006_priv.h"

char *ch7006_tv_norm_names[] = {
	[TV_NORM_PAL] = "PAL",
	[TV_NORM_PAL_M] = "PAL-M",
	[TV_NORM_PAL_N] = "PAL-N",
	[TV_NORM_PAL_NC] = "PAL-Nc",
	[TV_NORM_PAL_60] = "PAL-60",
	[TV_NORM_NTSC_M] = "NTSC-M",
	[TV_NORM_NTSC_J] = "NTSC-J",
};

#define NTSC_LIKE_TIMINGS .vrefresh = 60 * fixed1/1.001,		\
		.vdisplay = 480,					\
		.vtotal = 525,						\
		.hvirtual = 660

#define PAL_LIKE_TIMINGS .vrefresh = 50 * fixed1,		\
		.vdisplay = 576,				\
		.vtotal = 625,					\
		.hvirtual = 810

struct ch7006_tv_norm_info ch7006_tv_norms[] = {
	[TV_NORM_NTSC_M] = {
		NTSC_LIKE_TIMINGS,
		.black_level = 0.339 * fixed1,
		.subc_freq = 3579545 * fixed1,
		.dispmode = bitfs(CH7006_DISPMODE_OUTPUT_STD, NTSC),
		.voffset = 0,
	},
	[TV_NORM_NTSC_J] = {
		NTSC_LIKE_TIMINGS,
		.black_level = 0.286 * fixed1,
		.subc_freq = 3579545 * fixed1,
		.dispmode = bitfs(CH7006_DISPMODE_OUTPUT_STD, NTSC_J),
		.voffset = 0,
	},
	[TV_NORM_PAL] = {
		PAL_LIKE_TIMINGS,
		.black_level = 0.3 * fixed1,
		.subc_freq = 4433618.75 * fixed1,
		.dispmode = bitfs(CH7006_DISPMODE_OUTPUT_STD, PAL),
		.voffset = 0,
	},
	[TV_NORM_PAL_M] = {
		NTSC_LIKE_TIMINGS,
		.black_level = 0.339 * fixed1,
		.subc_freq = 3575611.433 * fixed1,
		.dispmode = bitfs(CH7006_DISPMODE_OUTPUT_STD, PAL_M),
		.voffset = 16,
	},

	/* The following modes seem to work right but they're
	 * undocumented */

	[TV_NORM_PAL_N] = {
		PAL_LIKE_TIMINGS,
		.black_level = 0.339 * fixed1,
		.subc_freq = 4433618.75 * fixed1,
		.dispmode = bitfs(CH7006_DISPMODE_OUTPUT_STD, PAL),
		.voffset = 0,
	},
	[TV_NORM_PAL_NC] = {
		PAL_LIKE_TIMINGS,
		.black_level = 0.3 * fixed1,
		.subc_freq = 3582056.25 * fixed1,
		.dispmode = bitfs(CH7006_DISPMODE_OUTPUT_STD, PAL),
		.voffset = 0,
	},
	[TV_NORM_PAL_60] = {
		NTSC_LIKE_TIMINGS,
		.black_level = 0.3 * fixed1,
		.subc_freq = 4433618.75 * fixed1,
		.dispmode = bitfs(CH7006_DISPMODE_OUTPUT_STD, PAL_M),
		.voffset = 16,
	},
};

#define __MODE(f, hd, vd, ht, vt, hsynp, vsynp,				\
	       subc, scale, scale_mask, norm_mask, e_hd, e_vd) {	\
		.mode = {						\
			.name = #hd "x" #vd,				\
			.status = 0,					\
			.type = DRM_MODE_TYPE_DRIVER,			\
			.clock = f,					\
			.hdisplay = hd,					\
			.hsync_start = e_hd + 16,			\
			.hsync_end = e_hd + 80,				\
			.htotal = ht,					\
			.hskew = 0,					\
			.vdisplay = vd,					\
			.vsync_start = vd + 10,				\
			.vsync_end = vd + 26,				\
			.vtotal = vt,					\
			.vscan = 0,					\
			.flags = DRM_MODE_FLAG_##hsynp##HSYNC |		\
				DRM_MODE_FLAG_##vsynp##VSYNC,		\
			.vrefresh = 0,					\
		},							\
		.enc_hdisp = e_hd,					\
		.enc_vdisp = e_vd,					\
		.subc_coeff = subc * fixed1,				\
		.dispmode = bitfs(CH7006_DISPMODE_SCALING_RATIO, scale) | \
			    bitfs(CH7006_DISPMODE_INPUT_RES, e_hd##x##e_vd), \
		.valid_scales = scale_mask,				\
		.valid_norms = norm_mask				\
	 }

#define MODE(f, hd, vd, ht, vt, hsynp, vsynp,				\
	     subc, scale, scale_mask, norm_mask)			\
	__MODE(f, hd, vd, ht, vt, hsynp, vsynp, subc, scale,		\
	       scale_mask, norm_mask, hd, vd)

#define NTSC_LIKE (1 << TV_NORM_NTSC_M | 1 << TV_NORM_NTSC_J |		\
		   1 << TV_NORM_PAL_M | 1 << TV_NORM_PAL_60)

#define PAL_LIKE (1 << TV_NORM_PAL | 1 << TV_NORM_PAL_N | 1 << TV_NORM_PAL_NC)

struct ch7006_mode ch7006_modes[] = {
	MODE(21000, 512, 384, 840, 500, N, N, 181.797557582, 5_4, 0x6, PAL_LIKE),
	MODE(26250, 512, 384, 840, 625, N, N, 145.438046066, 1_1, 0x1, PAL_LIKE),
	MODE(20140, 512, 384, 800, 420, N, N, 213.257083791, 5_4, 0x4, NTSC_LIKE),
	MODE(24671, 512, 384, 784, 525, N, N, 174.0874153, 1_1, 0x3, NTSC_LIKE),
	MODE(28125, 720, 400, 1125, 500, N, N, 135.742176298, 5_4, 0x6, PAL_LIKE),
	MODE(34875, 720, 400, 1116, 625, N, N, 109.469496898, 1_1, 0x1, PAL_LIKE),
	MODE(23790, 720, 400, 945, 420, N, N, 160.475642016, 5_4, 0x4, NTSC_LIKE),
	MODE(29455, 720, 400, 936, 525, N, N, 129.614941843, 1_1, 0x3, NTSC_LIKE),
	MODE(25000, 640, 400, 1000, 500, N, N, 152.709948279, 5_4, 0x6, PAL_LIKE),
	MODE(31500, 640, 400, 1008, 625, N, N, 121.198371646, 1_1, 0x1, PAL_LIKE),
	MODE(21147, 640, 400, 840, 420, N, N, 180.535097338, 5_4, 0x4, NTSC_LIKE),
	MODE(26434, 640, 400, 840, 525, N, N, 144.42807787, 1_1, 0x2, NTSC_LIKE),
	MODE(30210, 640, 400, 840, 600, N, N, 126.374568276, 7_8, 0x1, NTSC_LIKE),
	MODE(21000, 640, 480, 840, 500, N, N, 181.797557582, 5_4, 0x4, PAL_LIKE),
	MODE(26250, 640, 480, 840, 625, N, N, 145.438046066, 1_1, 0x2, PAL_LIKE),
	MODE(31500, 640, 480, 840, 750, N, N, 121.198371646, 5_6, 0x1, PAL_LIKE),
	MODE(24671, 640, 480, 784, 525, N, N, 174.0874153, 1_1, 0x4, NTSC_LIKE),
	MODE(28196, 640, 480, 784, 600, N, N, 152.326488422, 7_8, 0x2, NTSC_LIKE),
	MODE(30210, 640, 480, 800, 630, N, N, 142.171389101, 5_6, 0x1, NTSC_LIKE),
	__MODE(29500, 720, 576, 944, 625, P, P, 145.592111636, 1_1, 0x7, PAL_LIKE, 800, 600),
	MODE(36000, 800, 600, 960, 750, P, P, 119.304647022, 5_6, 0x6, PAL_LIKE),
	MODE(39000, 800, 600, 936, 836, P, P, 110.127366499, 3_4, 0x1, PAL_LIKE),
	MODE(39273, 800, 600, 1040, 630, P, P, 145.816809399, 5_6, 0x4, NTSC_LIKE),
	MODE(43636, 800, 600, 1040, 700, P, P, 131.235128487, 3_4, 0x2, NTSC_LIKE),
	MODE(47832, 800, 600, 1064, 750, P, P, 119.723275165, 7_10, 0x1, NTSC_LIKE),
	{}
};

struct ch7006_mode *ch7006_lookup_mode(struct drm_encoder *encoder,
				       struct drm_display_mode *drm_mode)
{
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	struct ch7006_mode *mode;

	for (mode = ch7006_modes; mode->mode.clock; mode++) {

		if (~mode->valid_norms & 1<<priv->norm)
			continue;

		if (mode->mode.hdisplay != drm_mode->hdisplay ||
		    mode->mode.vdisplay != drm_mode->vdisplay ||
		    mode->mode.vtotal != drm_mode->vtotal ||
		    mode->mode.htotal != drm_mode->htotal ||
		    mode->mode.clock != drm_mode->clock)
			continue;

		return mode;
	}

	return NULL;
}

/* Some common HW state calculation code */

void ch7006_setup_levels(struct drm_encoder *encoder)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	uint8_t *regs = priv->state.regs;
	struct ch7006_tv_norm_info *norm = &ch7006_tv_norms[priv->norm];
	int gain;
	int black_level;

	/* Set DAC_GAIN if the voltage drop between white and black is
	 * high enough. */
	if (norm->black_level < 339*fixed1/1000) {
		gain = 76;

		regs[CH7006_INPUT_FORMAT] |= CH7006_INPUT_FORMAT_DAC_GAIN;
	} else {
		gain = 71;

		regs[CH7006_INPUT_FORMAT] &= ~CH7006_INPUT_FORMAT_DAC_GAIN;
	}

	black_level = round_fixed(norm->black_level*26625)/gain;

	/* Correct it with the specified brightness. */
	black_level = interpolate(90, black_level, 208, priv->brightness);

	regs[CH7006_BLACK_LEVEL] = bitf(CH7006_BLACK_LEVEL_0, black_level);

	ch7006_dbg(client, "black level: %d\n", black_level);
}

void ch7006_setup_subcarrier(struct drm_encoder *encoder)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	struct ch7006_state *state = &priv->state;
	struct ch7006_tv_norm_info *norm = &ch7006_tv_norms[priv->norm];
	struct ch7006_mode *mode = priv->mode;
	uint32_t subc_inc;

	subc_inc = round_fixed((mode->subc_coeff >> 8)
			       * (norm->subc_freq >> 24));

	setbitf(state, CH7006_SUBC_INC0, 28, subc_inc);
	setbitf(state, CH7006_SUBC_INC1, 24, subc_inc);
	setbitf(state, CH7006_SUBC_INC2, 20, subc_inc);
	setbitf(state, CH7006_SUBC_INC3, 16, subc_inc);
	setbitf(state, CH7006_SUBC_INC4, 12, subc_inc);
	setbitf(state, CH7006_SUBC_INC5, 8, subc_inc);
	setbitf(state, CH7006_SUBC_INC6, 4, subc_inc);
	setbitf(state, CH7006_SUBC_INC7, 0, subc_inc);

	ch7006_dbg(client, "subcarrier inc: %u\n", subc_inc);
}

void ch7006_setup_pll(struct drm_encoder *encoder)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	uint8_t *regs = priv->state.regs;
	struct ch7006_mode *mode = priv->mode;
	int n, best_n = 0;
	int m, best_m = 0;
	int freq, best_freq = 0;

	for (n = 0; n < CH7006_MAXN; n++) {
		for (m = 0; m < CH7006_MAXM; m++) {
			freq = CH7006_FREQ0*(n+2)/(m+2);

			if (abs(freq - mode->mode.clock) <
			    abs(best_freq - mode->mode.clock)) {
				best_freq = freq;
				best_n = n;
				best_m = m;
			}
		}
	}

	regs[CH7006_PLLOV] = bitf(CH7006_PLLOV_N_8, best_n) |
		bitf(CH7006_PLLOV_M_8, best_m);

	regs[CH7006_PLLM] = bitf(CH7006_PLLM_0, best_m);
	regs[CH7006_PLLN] = bitf(CH7006_PLLN_0, best_n);

	if (best_n < 108)
		regs[CH7006_PLL_CONTROL] |= CH7006_PLL_CONTROL_CAPACITOR;
	else
		regs[CH7006_PLL_CONTROL] &= ~CH7006_PLL_CONTROL_CAPACITOR;

	ch7006_dbg(client, "n=%d m=%d f=%d c=%d\n",
		   best_n, best_m, best_freq, best_n < 108);
}

void ch7006_setup_power_state(struct drm_encoder *encoder)
{
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	uint8_t *power = &priv->state.regs[CH7006_POWER];
	int subconnector;

	subconnector = priv->select_subconnector ? priv->select_subconnector :
							priv->subconnector;

	*power = CH7006_POWER_RESET;

	if (priv->last_dpms == DRM_MODE_DPMS_ON) {
		switch (subconnector) {
		case DRM_MODE_SUBCONNECTOR_SVIDEO:
			*power |= bitfs(CH7006_POWER_LEVEL, CVBS_OFF);
			break;
		case DRM_MODE_SUBCONNECTOR_Composite:
			*power |= bitfs(CH7006_POWER_LEVEL, SVIDEO_OFF);
			break;
		case DRM_MODE_SUBCONNECTOR_SCART:
			*power |= bitfs(CH7006_POWER_LEVEL, NORMAL) |
				CH7006_POWER_SCART;
			break;
		}

	} else {
		*power |= bitfs(CH7006_POWER_LEVEL, FULL_POWER_OFF);
	}
}

void ch7006_setup_properties(struct drm_encoder *encoder)
{
	struct i2c_client *client = drm_i2c_encoder_get_client(encoder);
	struct ch7006_priv *priv = to_ch7006_priv(encoder);
	struct ch7006_state *state = &priv->state;
	struct ch7006_tv_norm_info *norm = &ch7006_tv_norms[priv->norm];
	struct ch7006_mode *ch_mode = priv->mode;
	struct drm_display_mode *mode = &ch_mode->mode;
	uint8_t *regs = state->regs;
	int flicker, contrast, hpos, vpos;
	uint64_t scale, aspect;

	flicker = interpolate(0, 2, 3, priv->flicker);
	regs[CH7006_FFILTER] = bitf(CH7006_FFILTER_TEXT, flicker) |
		bitf(CH7006_FFILTER_LUMA, flicker) |
		bitf(CH7006_FFILTER_CHROMA, 1);

	contrast = interpolate(0, 5, 7, priv->contrast);
	regs[CH7006_CONTRAST] = bitf(CH7006_CONTRAST_0, contrast);

	scale = norm->vtotal*fixed1;
	do_div(scale, mode->vtotal);

	aspect = ch_mode->enc_hdisp*fixed1;
	do_div(aspect, ch_mode->enc_vdisp);

	hpos = round_fixed((norm->hvirtual * aspect - mode->hdisplay * scale)
			   * priv->hmargin * mode->vtotal) / norm->vtotal / 100 / 4;

	setbitf(state, CH7006_POV, HPOS_8, hpos);
	setbitf(state, CH7006_HPOS, 0, hpos);

	vpos = max(0, norm->vdisplay - round_fixed(mode->vdisplay*scale)
		   + norm->voffset) * priv->vmargin / 100 / 2;

	setbitf(state, CH7006_POV, VPOS_8, vpos);
	setbitf(state, CH7006_VPOS, 0, vpos);

	ch7006_dbg(client, "hpos: %d, vpos: %d\n", hpos, vpos);
}

/* HW access functions */

void ch7006_write(struct i2c_client *client, uint8_t addr, uint8_t val)
{
	uint8_t buf[] = {addr, val};
	int ret;

	ret = i2c_master_send(client, buf, ARRAY_SIZE(buf));
	if (ret < 0)
		ch7006_err(client, "Error %d writing to subaddress 0x%x\n",
			   ret, addr);
}

uint8_t ch7006_read(struct i2c_client *client, uint8_t addr)
{
	uint8_t val;
	int ret;

	ret = i2c_master_send(client, &addr, sizeof(addr));
	if (ret < 0)
		goto fail;

	ret = i2c_master_recv(client, &val, sizeof(val));
	if (ret < 0)
		goto fail;

	return val;

fail:
	ch7006_err(client, "Error %d reading from subaddress 0x%x\n",
		   ret, addr);
	return 0;
}

void ch7006_state_load(struct i2c_client *client,
		       struct ch7006_state *state)
{
	ch7006_load_reg(client, state, CH7006_POWER);

	ch7006_load_reg(client, state, CH7006_DISPMODE);
	ch7006_load_reg(client, state, CH7006_FFILTER);
	ch7006_load_reg(client, state, CH7006_BWIDTH);
	ch7006_load_reg(client, state, CH7006_INPUT_FORMAT);
	ch7006_load_reg(client, state, CH7006_CLKMODE);
	ch7006_load_reg(client, state, CH7006_START_ACTIVE);
	ch7006_load_reg(client, state, CH7006_POV);
	ch7006_load_reg(client, state, CH7006_BLACK_LEVEL);
	ch7006_load_reg(client, state, CH7006_HPOS);
	ch7006_load_reg(client, state, CH7006_VPOS);
	ch7006_load_reg(client, state, CH7006_INPUT_SYNC);
	ch7006_load_reg(client, state, CH7006_DETECT);
	ch7006_load_reg(client, state, CH7006_CONTRAST);
	ch7006_load_reg(client, state, CH7006_PLLOV);
	ch7006_load_reg(client, state, CH7006_PLLM);
	ch7006_load_reg(client, state, CH7006_PLLN);
	ch7006_load_reg(client, state, CH7006_BCLKOUT);
	ch7006_load_reg(client, state, CH7006_SUBC_INC0);
	ch7006_load_reg(client, state, CH7006_SUBC_INC1);
	ch7006_load_reg(client, state, CH7006_SUBC_INC2);
	ch7006_load_reg(client, state, CH7006_SUBC_INC3);
	ch7006_load_reg(client, state, CH7006_SUBC_INC4);
	ch7006_load_reg(client, state, CH7006_SUBC_INC5);
	ch7006_load_reg(client, state, CH7006_SUBC_INC6);
	ch7006_load_reg(client, state, CH7006_SUBC_INC7);
	ch7006_load_reg(client, state, CH7006_PLL_CONTROL);
	ch7006_load_reg(client, state, CH7006_CALC_SUBC_INC0);
}

void ch7006_state_save(struct i2c_client *client,
		       struct ch7006_state *state)
{
	ch7006_save_reg(client, state, CH7006_POWER);

	ch7006_save_reg(client, state, CH7006_DISPMODE);
	ch7006_save_reg(client, state, CH7006_FFILTER);
	ch7006_save_reg(client, state, CH7006_BWIDTH);
	ch7006_save_reg(client, state, CH7006_INPUT_FORMAT);
	ch7006_save_reg(client, state, CH7006_CLKMODE);
	ch7006_save_reg(client, state, CH7006_START_ACTIVE);
	ch7006_save_reg(client, state, CH7006_POV);
	ch7006_save_reg(client, state, CH7006_BLACK_LEVEL);
	ch7006_save_reg(client, state, CH7006_HPOS);
	ch7006_save_reg(client, state, CH7006_VPOS);
	ch7006_save_reg(client, state, CH7006_INPUT_SYNC);
	ch7006_save_reg(client, state, CH7006_DETECT);
	ch7006_save_reg(client, state, CH7006_CONTRAST);
	ch7006_save_reg(client, state, CH7006_PLLOV);
	ch7006_save_reg(client, state, CH7006_PLLM);
	ch7006_save_reg(client, state, CH7006_PLLN);
	ch7006_save_reg(client, state, CH7006_BCLKOUT);
	ch7006_save_reg(client, state, CH7006_SUBC_INC0);
	ch7006_save_reg(client, state, CH7006_SUBC_INC1);
	ch7006_save_reg(client, state, CH7006_SUBC_INC2);
	ch7006_save_reg(client, state, CH7006_SUBC_INC3);
	ch7006_save_reg(client, state, CH7006_SUBC_INC4);
	ch7006_save_reg(client, state, CH7006_SUBC_INC5);
	ch7006_save_reg(client, state, CH7006_SUBC_INC6);
	ch7006_save_reg(client, state, CH7006_SUBC_INC7);
	ch7006_save_reg(client, state, CH7006_PLL_CONTROL);
	ch7006_save_reg(client, state, CH7006_CALC_SUBC_INC0);

	state->regs[CH7006_FFILTER] = (state->regs[CH7006_FFILTER] & 0xf0) |
		(state->regs[CH7006_FFILTER] & 0x0c) >> 2 |
		(state->regs[CH7006_FFILTER] & 0x03) << 2;
}

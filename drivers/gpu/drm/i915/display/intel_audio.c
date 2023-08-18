/*
 * Copyright © 2014 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/component.h>
#include <linux/kernel.h>

#include <drm/drm_edid.h>
#include <drm/i915_component.h>

#include "i915_drv.h"
#include "intel_atomic.h"
#include "intel_audio.h"
#include "intel_audio_regs.h"
#include "intel_cdclk.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_lpe_audio.h"

/**
 * DOC: High Definition Audio over HDMI and Display Port
 *
 * The graphics and audio drivers together support High Definition Audio over
 * HDMI and Display Port. The audio programming sequences are divided into audio
 * codec and controller enable and disable sequences. The graphics driver
 * handles the audio codec sequences, while the audio driver handles the audio
 * controller sequences.
 *
 * The disable sequences must be performed before disabling the transcoder or
 * port. The enable sequences may only be performed after enabling the
 * transcoder and port, and after completed link training. Therefore the audio
 * enable/disable sequences are part of the modeset sequence.
 *
 * The codec and controller sequences could be done either parallel or serial,
 * but generally the ELDV/PD change in the codec sequence indicates to the audio
 * driver that the controller sequence should start. Indeed, most of the
 * co-operation between the graphics and audio drivers is handled via audio
 * related registers. (The notable exception is the power management, not
 * covered here.)
 *
 * The struct &i915_audio_component is used to interact between the graphics
 * and audio drivers. The struct &i915_audio_component_ops @ops in it is
 * defined in graphics driver and called in audio driver. The
 * struct &i915_audio_component_audio_ops @audio_ops is called from i915 driver.
 */

struct intel_audio_funcs {
	void (*audio_codec_enable)(struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state);
	void (*audio_codec_disable)(struct intel_encoder *encoder,
				    const struct intel_crtc_state *old_crtc_state,
				    const struct drm_connector_state *old_conn_state);
	void (*audio_codec_get_config)(struct intel_encoder *encoder,
				       struct intel_crtc_state *crtc_state);
};

/* DP N/M table */
#define LC_810M	810000
#define LC_540M	540000
#define LC_270M	270000
#define LC_162M	162000

struct dp_aud_n_m {
	int sample_rate;
	int clock;
	u16 m;
	u16 n;
};

struct hdmi_aud_ncts {
	int sample_rate;
	int clock;
	int n;
	int cts;
};

/* Values according to DP 1.4 Table 2-104 */
static const struct dp_aud_n_m dp_aud_n_m[] = {
	{ 32000, LC_162M, 1024, 10125 },
	{ 44100, LC_162M, 784, 5625 },
	{ 48000, LC_162M, 512, 3375 },
	{ 64000, LC_162M, 2048, 10125 },
	{ 88200, LC_162M, 1568, 5625 },
	{ 96000, LC_162M, 1024, 3375 },
	{ 128000, LC_162M, 4096, 10125 },
	{ 176400, LC_162M, 3136, 5625 },
	{ 192000, LC_162M, 2048, 3375 },
	{ 32000, LC_270M, 1024, 16875 },
	{ 44100, LC_270M, 784, 9375 },
	{ 48000, LC_270M, 512, 5625 },
	{ 64000, LC_270M, 2048, 16875 },
	{ 88200, LC_270M, 1568, 9375 },
	{ 96000, LC_270M, 1024, 5625 },
	{ 128000, LC_270M, 4096, 16875 },
	{ 176400, LC_270M, 3136, 9375 },
	{ 192000, LC_270M, 2048, 5625 },
	{ 32000, LC_540M, 1024, 33750 },
	{ 44100, LC_540M, 784, 18750 },
	{ 48000, LC_540M, 512, 11250 },
	{ 64000, LC_540M, 2048, 33750 },
	{ 88200, LC_540M, 1568, 18750 },
	{ 96000, LC_540M, 1024, 11250 },
	{ 128000, LC_540M, 4096, 33750 },
	{ 176400, LC_540M, 3136, 18750 },
	{ 192000, LC_540M, 2048, 11250 },
	{ 32000, LC_810M, 1024, 50625 },
	{ 44100, LC_810M, 784, 28125 },
	{ 48000, LC_810M, 512, 16875 },
	{ 64000, LC_810M, 2048, 50625 },
	{ 88200, LC_810M, 1568, 28125 },
	{ 96000, LC_810M, 1024, 16875 },
	{ 128000, LC_810M, 4096, 50625 },
	{ 176400, LC_810M, 3136, 28125 },
	{ 192000, LC_810M, 2048, 16875 },
};

static const struct dp_aud_n_m *
audio_config_dp_get_n_m(const struct intel_crtc_state *crtc_state, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dp_aud_n_m); i++) {
		if (rate == dp_aud_n_m[i].sample_rate &&
		    crtc_state->port_clock == dp_aud_n_m[i].clock)
			return &dp_aud_n_m[i];
	}

	return NULL;
}

static const struct {
	int clock;
	u32 config;
} hdmi_audio_clock[] = {
	{ 25175, AUD_CONFIG_PIXEL_CLOCK_HDMI_25175 },
	{ 25200, AUD_CONFIG_PIXEL_CLOCK_HDMI_25200 }, /* default per bspec */
	{ 27000, AUD_CONFIG_PIXEL_CLOCK_HDMI_27000 },
	{ 27027, AUD_CONFIG_PIXEL_CLOCK_HDMI_27027 },
	{ 54000, AUD_CONFIG_PIXEL_CLOCK_HDMI_54000 },
	{ 54054, AUD_CONFIG_PIXEL_CLOCK_HDMI_54054 },
	{ 74176, AUD_CONFIG_PIXEL_CLOCK_HDMI_74176 },
	{ 74250, AUD_CONFIG_PIXEL_CLOCK_HDMI_74250 },
	{ 148352, AUD_CONFIG_PIXEL_CLOCK_HDMI_148352 },
	{ 148500, AUD_CONFIG_PIXEL_CLOCK_HDMI_148500 },
	{ 296703, AUD_CONFIG_PIXEL_CLOCK_HDMI_296703 },
	{ 297000, AUD_CONFIG_PIXEL_CLOCK_HDMI_297000 },
	{ 593407, AUD_CONFIG_PIXEL_CLOCK_HDMI_593407 },
	{ 594000, AUD_CONFIG_PIXEL_CLOCK_HDMI_594000 },
};

/* HDMI N/CTS table */
#define TMDS_297M 297000
#define TMDS_296M 296703
#define TMDS_594M 594000
#define TMDS_593M 593407

static const struct hdmi_aud_ncts hdmi_aud_ncts_24bpp[] = {
	{ 32000, TMDS_296M, 5824, 421875 },
	{ 32000, TMDS_297M, 3072, 222750 },
	{ 32000, TMDS_593M, 5824, 843750 },
	{ 32000, TMDS_594M, 3072, 445500 },
	{ 44100, TMDS_296M, 4459, 234375 },
	{ 44100, TMDS_297M, 4704, 247500 },
	{ 44100, TMDS_593M, 8918, 937500 },
	{ 44100, TMDS_594M, 9408, 990000 },
	{ 88200, TMDS_296M, 8918, 234375 },
	{ 88200, TMDS_297M, 9408, 247500 },
	{ 88200, TMDS_593M, 17836, 937500 },
	{ 88200, TMDS_594M, 18816, 990000 },
	{ 176400, TMDS_296M, 17836, 234375 },
	{ 176400, TMDS_297M, 18816, 247500 },
	{ 176400, TMDS_593M, 35672, 937500 },
	{ 176400, TMDS_594M, 37632, 990000 },
	{ 48000, TMDS_296M, 5824, 281250 },
	{ 48000, TMDS_297M, 5120, 247500 },
	{ 48000, TMDS_593M, 5824, 562500 },
	{ 48000, TMDS_594M, 6144, 594000 },
	{ 96000, TMDS_296M, 11648, 281250 },
	{ 96000, TMDS_297M, 10240, 247500 },
	{ 96000, TMDS_593M, 11648, 562500 },
	{ 96000, TMDS_594M, 12288, 594000 },
	{ 192000, TMDS_296M, 23296, 281250 },
	{ 192000, TMDS_297M, 20480, 247500 },
	{ 192000, TMDS_593M, 23296, 562500 },
	{ 192000, TMDS_594M, 24576, 594000 },
};

/* Appendix C - N & CTS values for deep color from HDMI 2.0 spec*/
/* HDMI N/CTS table for 10 bit deep color(30 bpp)*/
#define TMDS_371M 371250
#define TMDS_370M 370878

static const struct hdmi_aud_ncts hdmi_aud_ncts_30bpp[] = {
	{ 32000, TMDS_370M, 5824, 527344 },
	{ 32000, TMDS_371M, 6144, 556875 },
	{ 44100, TMDS_370M, 8918, 585938 },
	{ 44100, TMDS_371M, 4704, 309375 },
	{ 88200, TMDS_370M, 17836, 585938 },
	{ 88200, TMDS_371M, 9408, 309375 },
	{ 176400, TMDS_370M, 35672, 585938 },
	{ 176400, TMDS_371M, 18816, 309375 },
	{ 48000, TMDS_370M, 11648, 703125 },
	{ 48000, TMDS_371M, 5120, 309375 },
	{ 96000, TMDS_370M, 23296, 703125 },
	{ 96000, TMDS_371M, 10240, 309375 },
	{ 192000, TMDS_370M, 46592, 703125 },
	{ 192000, TMDS_371M, 20480, 309375 },
};

/* HDMI N/CTS table for 12 bit deep color(36 bpp)*/
#define TMDS_445_5M 445500
#define TMDS_445M 445054

static const struct hdmi_aud_ncts hdmi_aud_ncts_36bpp[] = {
	{ 32000, TMDS_445M, 5824, 632813 },
	{ 32000, TMDS_445_5M, 4096, 445500 },
	{ 44100, TMDS_445M, 8918, 703125 },
	{ 44100, TMDS_445_5M, 4704, 371250 },
	{ 88200, TMDS_445M, 17836, 703125 },
	{ 88200, TMDS_445_5M, 9408, 371250 },
	{ 176400, TMDS_445M, 35672, 703125 },
	{ 176400, TMDS_445_5M, 18816, 371250 },
	{ 48000, TMDS_445M, 5824, 421875 },
	{ 48000, TMDS_445_5M, 5120, 371250 },
	{ 96000, TMDS_445M, 11648, 421875 },
	{ 96000, TMDS_445_5M, 10240, 371250 },
	{ 192000, TMDS_445M, 23296, 421875 },
	{ 192000, TMDS_445_5M, 20480, 371250 },
};

/* get AUD_CONFIG_PIXEL_CLOCK_HDMI_* value for mode */
static u32 audio_config_hdmi_pixel_clock(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_audio_clock); i++) {
		if (adjusted_mode->crtc_clock == hdmi_audio_clock[i].clock)
			break;
	}

	if (DISPLAY_VER(i915) < 12 && adjusted_mode->crtc_clock > 148500)
		i = ARRAY_SIZE(hdmi_audio_clock);

	if (i == ARRAY_SIZE(hdmi_audio_clock)) {
		drm_dbg_kms(&i915->drm,
			    "HDMI audio pixel clock setting for %d not found, falling back to defaults\n",
			    adjusted_mode->crtc_clock);
		i = 1;
	}

	drm_dbg_kms(&i915->drm,
		    "Configuring HDMI audio for pixel clock %d (0x%08x)\n",
		    hdmi_audio_clock[i].clock,
		    hdmi_audio_clock[i].config);

	return hdmi_audio_clock[i].config;
}

static int audio_config_hdmi_get_n(const struct intel_crtc_state *crtc_state,
				   int rate)
{
	const struct hdmi_aud_ncts *hdmi_ncts_table;
	int i, size;

	if (crtc_state->pipe_bpp == 36) {
		hdmi_ncts_table = hdmi_aud_ncts_36bpp;
		size = ARRAY_SIZE(hdmi_aud_ncts_36bpp);
	} else if (crtc_state->pipe_bpp == 30) {
		hdmi_ncts_table = hdmi_aud_ncts_30bpp;
		size = ARRAY_SIZE(hdmi_aud_ncts_30bpp);
	} else {
		hdmi_ncts_table = hdmi_aud_ncts_24bpp;
		size = ARRAY_SIZE(hdmi_aud_ncts_24bpp);
	}

	for (i = 0; i < size; i++) {
		if (rate == hdmi_ncts_table[i].sample_rate &&
		    crtc_state->port_clock == hdmi_ncts_table[i].clock) {
			return hdmi_ncts_table[i].n;
		}
	}
	return 0;
}

/* ELD buffer size in dwords */
static int g4x_eld_buffer_size(struct drm_i915_private *i915)
{
	u32 tmp;

	tmp = intel_de_read(i915, G4X_AUD_CNTL_ST);

	return REG_FIELD_GET(G4X_ELD_BUFFER_SIZE_MASK, tmp);
}

static void g4x_audio_codec_get_config(struct intel_encoder *encoder,
				       struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	u32 *eld = (u32 *)crtc_state->eld;
	int eld_buffer_size, len, i;
	u32 tmp;

	tmp = intel_de_read(i915, G4X_AUD_CNTL_ST);
	if ((tmp & G4X_ELD_VALID) == 0)
		return;

	intel_de_rmw(i915, G4X_AUD_CNTL_ST, G4X_ELD_ADDRESS_MASK, 0);

	eld_buffer_size = g4x_eld_buffer_size(i915);
	len = min_t(int, sizeof(crtc_state->eld) / 4, eld_buffer_size);

	for (i = 0; i < len; i++)
		eld[i] = intel_de_read(i915, G4X_HDMIW_HDMIEDID);
}

static void g4x_audio_codec_disable(struct intel_encoder *encoder,
				    const struct intel_crtc_state *old_crtc_state,
				    const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);

	/* Invalidate ELD */
	intel_de_rmw(i915, G4X_AUD_CNTL_ST,
		     G4X_ELD_VALID, 0);

	intel_crtc_wait_for_next_vblank(crtc);
	intel_crtc_wait_for_next_vblank(crtc);
}

static void g4x_audio_codec_enable(struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const u32 *eld = (const u32 *)crtc_state->eld;
	int eld_buffer_size, len, i;

	intel_crtc_wait_for_next_vblank(crtc);

	intel_de_rmw(i915, G4X_AUD_CNTL_ST,
		     G4X_ELD_VALID | G4X_ELD_ADDRESS_MASK, 0);

	eld_buffer_size = g4x_eld_buffer_size(i915);
	len = min(drm_eld_size(crtc_state->eld) / 4, eld_buffer_size);

	for (i = 0; i < len; i++)
		intel_de_write(i915, G4X_HDMIW_HDMIEDID, eld[i]);
	for (; i < eld_buffer_size; i++)
		intel_de_write(i915, G4X_HDMIW_HDMIEDID, 0);

	drm_WARN_ON(&i915->drm,
		    (intel_de_read(i915, G4X_AUD_CNTL_ST) & G4X_ELD_ADDRESS_MASK) != 0);

	intel_de_rmw(i915, G4X_AUD_CNTL_ST,
		     0, G4X_ELD_VALID);
}

static void
hsw_dp_audio_config_update(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct i915_audio_component *acomp = i915->display.audio.component;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum port port = encoder->port;
	const struct dp_aud_n_m *nm;
	int rate;
	u32 tmp;

	rate = acomp ? acomp->aud_sample_rate[port] : 0;
	nm = audio_config_dp_get_n_m(crtc_state, rate);
	if (nm)
		drm_dbg_kms(&i915->drm, "using Maud %u, Naud %u\n", nm->m,
			    nm->n);
	else
		drm_dbg_kms(&i915->drm, "using automatic Maud, Naud\n");

	tmp = intel_de_read(i915, HSW_AUD_CFG(cpu_transcoder));
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp &= ~AUD_CONFIG_PIXEL_CLOCK_HDMI_MASK;
	tmp &= ~AUD_CONFIG_N_PROG_ENABLE;
	tmp |= AUD_CONFIG_N_VALUE_INDEX;

	if (nm) {
		tmp &= ~AUD_CONFIG_N_MASK;
		tmp |= AUD_CONFIG_N(nm->n);
		tmp |= AUD_CONFIG_N_PROG_ENABLE;
	}

	intel_de_write(i915, HSW_AUD_CFG(cpu_transcoder), tmp);

	tmp = intel_de_read(i915, HSW_AUD_M_CTS_ENABLE(cpu_transcoder));
	tmp &= ~AUD_CONFIG_M_MASK;
	tmp &= ~AUD_M_CTS_M_VALUE_INDEX;
	tmp &= ~AUD_M_CTS_M_PROG_ENABLE;

	if (nm) {
		tmp |= nm->m;
		tmp |= AUD_M_CTS_M_VALUE_INDEX;
		tmp |= AUD_M_CTS_M_PROG_ENABLE;
	}

	intel_de_write(i915, HSW_AUD_M_CTS_ENABLE(cpu_transcoder), tmp);
}

static void
hsw_hdmi_audio_config_update(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct i915_audio_component *acomp = i915->display.audio.component;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum port port = encoder->port;
	int n, rate;
	u32 tmp;

	rate = acomp ? acomp->aud_sample_rate[port] : 0;

	tmp = intel_de_read(i915, HSW_AUD_CFG(cpu_transcoder));
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp &= ~AUD_CONFIG_PIXEL_CLOCK_HDMI_MASK;
	tmp &= ~AUD_CONFIG_N_PROG_ENABLE;
	tmp |= audio_config_hdmi_pixel_clock(crtc_state);

	n = audio_config_hdmi_get_n(crtc_state, rate);
	if (n != 0) {
		drm_dbg_kms(&i915->drm, "using N %d\n", n);

		tmp &= ~AUD_CONFIG_N_MASK;
		tmp |= AUD_CONFIG_N(n);
		tmp |= AUD_CONFIG_N_PROG_ENABLE;
	} else {
		drm_dbg_kms(&i915->drm, "using automatic N\n");
	}

	intel_de_write(i915, HSW_AUD_CFG(cpu_transcoder), tmp);

	/*
	 * Let's disable "Enable CTS or M Prog bit"
	 * and let HW calculate the value
	 */
	tmp = intel_de_read(i915, HSW_AUD_M_CTS_ENABLE(cpu_transcoder));
	tmp &= ~AUD_M_CTS_M_PROG_ENABLE;
	tmp &= ~AUD_M_CTS_M_VALUE_INDEX;
	intel_de_write(i915, HSW_AUD_M_CTS_ENABLE(cpu_transcoder), tmp);
}

static void
hsw_audio_config_update(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state)
{
	if (intel_crtc_has_dp_encoder(crtc_state))
		hsw_dp_audio_config_update(encoder, crtc_state);
	else
		hsw_hdmi_audio_config_update(encoder, crtc_state);
}

static void hsw_audio_codec_disable(struct intel_encoder *encoder,
				    const struct intel_crtc_state *old_crtc_state,
				    const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;

	mutex_lock(&i915->display.audio.mutex);

	/* Disable timestamps */
	intel_de_rmw(i915, HSW_AUD_CFG(cpu_transcoder),
		     AUD_CONFIG_N_VALUE_INDEX |
		     AUD_CONFIG_UPPER_N_MASK |
		     AUD_CONFIG_LOWER_N_MASK,
		     AUD_CONFIG_N_PROG_ENABLE |
		     (intel_crtc_has_dp_encoder(old_crtc_state) ?
		      AUD_CONFIG_N_VALUE_INDEX : 0));

	/* Invalidate ELD */
	intel_de_rmw(i915, HSW_AUD_PIN_ELD_CP_VLD,
		     AUDIO_ELD_VALID(cpu_transcoder), 0);

	intel_crtc_wait_for_next_vblank(crtc);
	intel_crtc_wait_for_next_vblank(crtc);

	/* Disable audio presence detect */
	intel_de_rmw(i915, HSW_AUD_PIN_ELD_CP_VLD,
		     AUDIO_OUTPUT_ENABLE(cpu_transcoder), 0);

	mutex_unlock(&i915->display.audio.mutex);
}

static unsigned int calc_hblank_early_prog(struct intel_encoder *encoder,
					   const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	unsigned int link_clks_available, link_clks_required;
	unsigned int tu_data, tu_line, link_clks_active;
	unsigned int h_active, h_total, hblank_delta, pixel_clk;
	unsigned int fec_coeff, cdclk, vdsc_bpp;
	unsigned int link_clk, lanes;
	unsigned int hblank_rise;

	h_active = crtc_state->hw.adjusted_mode.crtc_hdisplay;
	h_total = crtc_state->hw.adjusted_mode.crtc_htotal;
	pixel_clk = crtc_state->hw.adjusted_mode.crtc_clock;
	vdsc_bpp = crtc_state->dsc.compressed_bpp;
	cdclk = i915->display.cdclk.hw.cdclk;
	/* fec= 0.972261, using rounding multiplier of 1000000 */
	fec_coeff = 972261;
	link_clk = crtc_state->port_clock;
	lanes = crtc_state->lane_count;

	drm_dbg_kms(&i915->drm, "h_active = %u link_clk = %u :"
		    "lanes = %u vdsc_bpp = %u cdclk = %u\n",
		    h_active, link_clk, lanes, vdsc_bpp, cdclk);

	if (WARN_ON(!link_clk || !pixel_clk || !lanes || !vdsc_bpp || !cdclk))
		return 0;

	link_clks_available = (h_total - h_active) * link_clk / pixel_clk - 28;
	link_clks_required = DIV_ROUND_UP(192000 * h_total, 1000 * pixel_clk) * (48 / lanes + 2);

	if (link_clks_available > link_clks_required)
		hblank_delta = 32;
	else
		hblank_delta = DIV64_U64_ROUND_UP(mul_u32_u32(5 * (link_clk + cdclk), pixel_clk),
						  mul_u32_u32(link_clk, cdclk));

	tu_data = div64_u64(mul_u32_u32(pixel_clk * vdsc_bpp * 8, 1000000),
			    mul_u32_u32(link_clk * lanes, fec_coeff));
	tu_line = div64_u64(h_active * mul_u32_u32(link_clk, fec_coeff),
			    mul_u32_u32(64 * pixel_clk, 1000000));
	link_clks_active  = (tu_line - 1) * 64 + tu_data;

	hblank_rise = (link_clks_active + 6 * DIV_ROUND_UP(link_clks_active, 250) + 4) * pixel_clk / link_clk;

	return h_active - hblank_rise + hblank_delta;
}

static unsigned int calc_samples_room(const struct intel_crtc_state *crtc_state)
{
	unsigned int h_active, h_total, pixel_clk;
	unsigned int link_clk, lanes;

	h_active = crtc_state->hw.adjusted_mode.hdisplay;
	h_total = crtc_state->hw.adjusted_mode.htotal;
	pixel_clk = crtc_state->hw.adjusted_mode.clock;
	link_clk = crtc_state->port_clock;
	lanes = crtc_state->lane_count;

	return ((h_total - h_active) * link_clk - 12 * pixel_clk) /
		(pixel_clk * (48 / lanes + 2));
}

static void enable_audio_dsc_wa(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	unsigned int hblank_early_prog, samples_room;
	unsigned int val;

	if (DISPLAY_VER(i915) < 11)
		return;

	val = intel_de_read(i915, AUD_CONFIG_BE);

	if (DISPLAY_VER(i915) == 11)
		val |= HBLANK_EARLY_ENABLE_ICL(cpu_transcoder);
	else if (DISPLAY_VER(i915) >= 12)
		val |= HBLANK_EARLY_ENABLE_TGL(cpu_transcoder);

	if (crtc_state->dsc.compression_enable &&
	    crtc_state->hw.adjusted_mode.hdisplay >= 3840 &&
	    crtc_state->hw.adjusted_mode.vdisplay >= 2160) {
		/* Get hblank early enable value required */
		val &= ~HBLANK_START_COUNT_MASK(cpu_transcoder);
		hblank_early_prog = calc_hblank_early_prog(encoder, crtc_state);
		if (hblank_early_prog < 32)
			val |= HBLANK_START_COUNT(cpu_transcoder, HBLANK_START_COUNT_32);
		else if (hblank_early_prog < 64)
			val |= HBLANK_START_COUNT(cpu_transcoder, HBLANK_START_COUNT_64);
		else if (hblank_early_prog < 96)
			val |= HBLANK_START_COUNT(cpu_transcoder, HBLANK_START_COUNT_96);
		else
			val |= HBLANK_START_COUNT(cpu_transcoder, HBLANK_START_COUNT_128);

		/* Get samples room value required */
		val &= ~NUMBER_SAMPLES_PER_LINE_MASK(cpu_transcoder);
		samples_room = calc_samples_room(crtc_state);
		if (samples_room < 3)
			val |= NUMBER_SAMPLES_PER_LINE(cpu_transcoder, samples_room);
		else /* Program 0 i.e "All Samples available in buffer" */
			val |= NUMBER_SAMPLES_PER_LINE(cpu_transcoder, 0x0);
	}

	intel_de_write(i915, AUD_CONFIG_BE, val);
}

static void hsw_audio_codec_enable(struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	mutex_lock(&i915->display.audio.mutex);

	/* Enable Audio WA for 4k DSC usecases */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP))
		enable_audio_dsc_wa(encoder, crtc_state);

	/* Enable audio presence detect */
	intel_de_rmw(i915, HSW_AUD_PIN_ELD_CP_VLD,
		     0, AUDIO_OUTPUT_ENABLE(cpu_transcoder));

	intel_crtc_wait_for_next_vblank(crtc);

	/* Invalidate ELD */
	intel_de_rmw(i915, HSW_AUD_PIN_ELD_CP_VLD,
		     AUDIO_ELD_VALID(cpu_transcoder), 0);

	/*
	 * The audio componenent is used to convey the ELD
	 * instead using of the hardware ELD buffer.
	 */

	/* Enable timestamps */
	hsw_audio_config_update(encoder, crtc_state);

	mutex_unlock(&i915->display.audio.mutex);
}

struct ibx_audio_regs {
	i915_reg_t hdmiw_hdmiedid, aud_config, aud_cntl_st, aud_cntrl_st2;
};

static void ibx_audio_regs_init(struct drm_i915_private *i915,
				enum pipe pipe,
				struct ibx_audio_regs *regs)
{
	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915)) {
		regs->hdmiw_hdmiedid = VLV_HDMIW_HDMIEDID(pipe);
		regs->aud_config = VLV_AUD_CFG(pipe);
		regs->aud_cntl_st = VLV_AUD_CNTL_ST(pipe);
		regs->aud_cntrl_st2 = VLV_AUD_CNTL_ST2;
	} else if (HAS_PCH_CPT(i915)) {
		regs->hdmiw_hdmiedid = CPT_HDMIW_HDMIEDID(pipe);
		regs->aud_config = CPT_AUD_CFG(pipe);
		regs->aud_cntl_st = CPT_AUD_CNTL_ST(pipe);
		regs->aud_cntrl_st2 = CPT_AUD_CNTRL_ST2;
	} else if (HAS_PCH_IBX(i915)) {
		regs->hdmiw_hdmiedid = IBX_HDMIW_HDMIEDID(pipe);
		regs->aud_config = IBX_AUD_CFG(pipe);
		regs->aud_cntl_st = IBX_AUD_CNTL_ST(pipe);
		regs->aud_cntrl_st2 = IBX_AUD_CNTL_ST2;
	}
}

static void ibx_audio_codec_disable(struct intel_encoder *encoder,
				    const struct intel_crtc_state *old_crtc_state,
				    const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	enum port port = encoder->port;
	enum pipe pipe = crtc->pipe;
	struct ibx_audio_regs regs;

	if (drm_WARN_ON(&i915->drm, port == PORT_A))
		return;

	ibx_audio_regs_init(i915, pipe, &regs);

	mutex_lock(&i915->display.audio.mutex);

	/* Disable timestamps */
	intel_de_rmw(i915, regs.aud_config,
		     AUD_CONFIG_N_VALUE_INDEX |
		     AUD_CONFIG_UPPER_N_MASK |
		     AUD_CONFIG_LOWER_N_MASK,
		     AUD_CONFIG_N_PROG_ENABLE |
		     (intel_crtc_has_dp_encoder(old_crtc_state) ?
		      AUD_CONFIG_N_VALUE_INDEX : 0));

	/* Invalidate ELD */
	intel_de_rmw(i915, regs.aud_cntrl_st2,
		     IBX_ELD_VALID(port), 0);

	mutex_unlock(&i915->display.audio.mutex);

	intel_crtc_wait_for_next_vblank(crtc);
	intel_crtc_wait_for_next_vblank(crtc);
}

static void ibx_audio_codec_enable(struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum port port = encoder->port;
	enum pipe pipe = crtc->pipe;
	struct ibx_audio_regs regs;

	if (drm_WARN_ON(&i915->drm, port == PORT_A))
		return;

	intel_crtc_wait_for_next_vblank(crtc);

	ibx_audio_regs_init(i915, pipe, &regs);

	mutex_lock(&i915->display.audio.mutex);

	/* Invalidate ELD */
	intel_de_rmw(i915, regs.aud_cntrl_st2,
		     IBX_ELD_VALID(port), 0);

	/*
	 * The audio componenent is used to convey the ELD
	 * instead using of the hardware ELD buffer.
	 */

	/* Enable timestamps */
	intel_de_rmw(i915, regs.aud_config,
		     AUD_CONFIG_N_VALUE_INDEX |
		     AUD_CONFIG_N_PROG_ENABLE |
		     AUD_CONFIG_PIXEL_CLOCK_HDMI_MASK,
		     (intel_crtc_has_dp_encoder(crtc_state) ?
		      AUD_CONFIG_N_VALUE_INDEX :
		      audio_config_hdmi_pixel_clock(crtc_state)));

	mutex_unlock(&i915->display.audio.mutex);
}

void intel_audio_sdp_split_update(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum transcoder trans = crtc_state->cpu_transcoder;

	if (HAS_DP20(i915))
		intel_de_rmw(i915, AUD_DP_2DOT0_CTRL(trans), AUD_ENABLE_SDP_SPLIT,
			     crtc_state->sdp_split_enable ? AUD_ENABLE_SDP_SPLIT : 0);
}

bool intel_audio_compute_config(struct intel_encoder *encoder,
				struct intel_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct drm_connector *connector = conn_state->connector;
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	if (!connector->eld[0]) {
		drm_dbg_kms(&i915->drm,
			    "Bogus ELD on [CONNECTOR:%d:%s]\n",
			    connector->base.id, connector->name);
		return false;
	}

	BUILD_BUG_ON(sizeof(crtc_state->eld) != sizeof(connector->eld));
	memcpy(crtc_state->eld, connector->eld, sizeof(crtc_state->eld));

	crtc_state->eld[6] = drm_av_sync_delay(connector, adjusted_mode) / 2;

	return true;
}

/**
 * intel_audio_codec_enable - Enable the audio codec for HD audio
 * @encoder: encoder on which to enable audio
 * @crtc_state: pointer to the current crtc state.
 * @conn_state: pointer to the current connector state.
 *
 * The enable sequences may only be performed after enabling the transcoder and
 * port, and after completed link training.
 */
void intel_audio_codec_enable(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state,
			      const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct i915_audio_component *acomp = i915->display.audio.component;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	struct intel_audio_state *audio_state;
	enum port port = encoder->port;

	if (!crtc_state->has_audio)
		return;

	drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s][ENCODER:%d:%s] Enable audio codec on [CRTC:%d:%s], %u bytes ELD\n",
		    connector->base.base.id, connector->base.name,
		    encoder->base.base.id, encoder->base.name,
		    crtc->base.base.id, crtc->base.name,
		    drm_eld_size(crtc_state->eld));

	if (i915->display.funcs.audio)
		i915->display.funcs.audio->audio_codec_enable(encoder,
							      crtc_state,
							      conn_state);

	mutex_lock(&i915->display.audio.mutex);

	audio_state = &i915->display.audio.state[cpu_transcoder];

	audio_state->encoder = encoder;
	BUILD_BUG_ON(sizeof(audio_state->eld) != sizeof(crtc_state->eld));
	memcpy(audio_state->eld, crtc_state->eld, sizeof(audio_state->eld));

	mutex_unlock(&i915->display.audio.mutex);

	if (acomp && acomp->base.audio_ops &&
	    acomp->base.audio_ops->pin_eld_notify) {
		/* audio drivers expect cpu_transcoder = -1 to indicate Non-MST cases */
		if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST))
			cpu_transcoder = -1;
		acomp->base.audio_ops->pin_eld_notify(acomp->base.audio_ops->audio_ptr,
						      (int)port, (int)cpu_transcoder);
	}

	intel_lpe_audio_notify(i915, cpu_transcoder, port, crtc_state->eld,
			       crtc_state->port_clock,
			       intel_crtc_has_dp_encoder(crtc_state));
}

/**
 * intel_audio_codec_disable - Disable the audio codec for HD audio
 * @encoder: encoder on which to disable audio
 * @old_crtc_state: pointer to the old crtc state.
 * @old_conn_state: pointer to the old connector state.
 *
 * The disable sequences must be performed before disabling the transcoder or
 * port.
 */
void intel_audio_codec_disable(struct intel_encoder *encoder,
			       const struct intel_crtc_state *old_crtc_state,
			       const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct i915_audio_component *acomp = i915->display.audio.component;
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	struct intel_connector *connector = to_intel_connector(old_conn_state->connector);
	enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;
	struct intel_audio_state *audio_state;
	enum port port = encoder->port;

	if (!old_crtc_state->has_audio)
		return;

	drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s][ENCODER:%d:%s] Disable audio codec on [CRTC:%d:%s]\n",
		    connector->base.base.id, connector->base.name,
		    encoder->base.base.id, encoder->base.name,
		    crtc->base.base.id, crtc->base.name);

	if (i915->display.funcs.audio)
		i915->display.funcs.audio->audio_codec_disable(encoder,
							       old_crtc_state,
							       old_conn_state);

	mutex_lock(&i915->display.audio.mutex);

	audio_state = &i915->display.audio.state[cpu_transcoder];

	audio_state->encoder = NULL;
	memset(audio_state->eld, 0, sizeof(audio_state->eld));

	mutex_unlock(&i915->display.audio.mutex);

	if (acomp && acomp->base.audio_ops &&
	    acomp->base.audio_ops->pin_eld_notify) {
		/* audio drivers expect cpu_transcoder = -1 to indicate Non-MST cases */
		if (!intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_DP_MST))
			cpu_transcoder = -1;
		acomp->base.audio_ops->pin_eld_notify(acomp->base.audio_ops->audio_ptr,
						      (int)port, (int)cpu_transcoder);
	}

	intel_lpe_audio_notify(i915, cpu_transcoder, port, NULL, 0, false);
}

static void intel_acomp_get_config(struct intel_encoder *encoder,
				   struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	struct intel_audio_state *audio_state;

	mutex_lock(&i915->display.audio.mutex);

	audio_state = &i915->display.audio.state[cpu_transcoder];

	if (audio_state->encoder)
		memcpy(crtc_state->eld, audio_state->eld, sizeof(audio_state->eld));

	mutex_unlock(&i915->display.audio.mutex);
}

void intel_audio_codec_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	if (!crtc_state->has_audio)
		return;

	if (i915->display.funcs.audio)
		i915->display.funcs.audio->audio_codec_get_config(encoder, crtc_state);
}

static const struct intel_audio_funcs g4x_audio_funcs = {
	.audio_codec_enable = g4x_audio_codec_enable,
	.audio_codec_disable = g4x_audio_codec_disable,
	.audio_codec_get_config = g4x_audio_codec_get_config,
};

static const struct intel_audio_funcs ibx_audio_funcs = {
	.audio_codec_enable = ibx_audio_codec_enable,
	.audio_codec_disable = ibx_audio_codec_disable,
	.audio_codec_get_config = intel_acomp_get_config,
};

static const struct intel_audio_funcs hsw_audio_funcs = {
	.audio_codec_enable = hsw_audio_codec_enable,
	.audio_codec_disable = hsw_audio_codec_disable,
	.audio_codec_get_config = intel_acomp_get_config,
};

/**
 * intel_audio_hooks_init - Set up chip specific audio hooks
 * @i915: device private
 */
void intel_audio_hooks_init(struct drm_i915_private *i915)
{
	if (IS_G4X(i915))
		i915->display.funcs.audio = &g4x_audio_funcs;
	else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915) ||
		 HAS_PCH_CPT(i915) || HAS_PCH_IBX(i915))
		i915->display.funcs.audio = &ibx_audio_funcs;
	else if (IS_HASWELL(i915) || DISPLAY_VER(i915) >= 8)
		i915->display.funcs.audio = &hsw_audio_funcs;
}

struct aud_ts_cdclk_m_n {
	u8 m;
	u16 n;
};

void intel_audio_cdclk_change_pre(struct drm_i915_private *i915)
{
	if (DISPLAY_VER(i915) >= 13)
		intel_de_rmw(i915, AUD_TS_CDCLK_M, AUD_TS_CDCLK_M_EN, 0);
}

static void get_aud_ts_cdclk_m_n(int refclk, int cdclk, struct aud_ts_cdclk_m_n *aud_ts)
{
	aud_ts->m = 60;
	aud_ts->n = cdclk * aud_ts->m / 24000;
}

void intel_audio_cdclk_change_post(struct drm_i915_private *i915)
{
	struct aud_ts_cdclk_m_n aud_ts;

	if (DISPLAY_VER(i915) >= 13) {
		get_aud_ts_cdclk_m_n(i915->display.cdclk.hw.ref, i915->display.cdclk.hw.cdclk, &aud_ts);

		intel_de_write(i915, AUD_TS_CDCLK_N, aud_ts.n);
		intel_de_write(i915, AUD_TS_CDCLK_M, aud_ts.m | AUD_TS_CDCLK_M_EN);
		drm_dbg_kms(&i915->drm, "aud_ts_cdclk set to M=%u, N=%u\n", aud_ts.m, aud_ts.n);
	}
}

static int glk_force_audio_cdclk_commit(struct intel_atomic_state *state,
					struct intel_crtc *crtc,
					bool enable)
{
	struct intel_cdclk_state *cdclk_state;
	int ret;

	/* need to hold at least one crtc lock for the global state */
	ret = drm_modeset_lock(&crtc->base.mutex, state->base.acquire_ctx);
	if (ret)
		return ret;

	cdclk_state = intel_atomic_get_cdclk_state(state);
	if (IS_ERR(cdclk_state))
		return PTR_ERR(cdclk_state);

	cdclk_state->force_min_cdclk = enable ? 2 * 96000 : 0;

	return drm_atomic_commit(&state->base);
}

static void glk_force_audio_cdclk(struct drm_i915_private *i915,
				  bool enable)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *state;
	struct intel_crtc *crtc;
	int ret;

	crtc = intel_first_crtc(i915);
	if (!crtc)
		return;

	drm_modeset_acquire_init(&ctx, 0);
	state = drm_atomic_state_alloc(&i915->drm);
	if (drm_WARN_ON(&i915->drm, !state))
		return;

	state->acquire_ctx = &ctx;
	to_intel_atomic_state(state)->internal = true;

retry:
	ret = glk_force_audio_cdclk_commit(to_intel_atomic_state(state), crtc,
					   enable);
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_WARN_ON(&i915->drm, ret);

	drm_atomic_state_put(state);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

static unsigned long i915_audio_component_get_power(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);
	intel_wakeref_t ret;

	/* Catch potential impedance mismatches before they occur! */
	BUILD_BUG_ON(sizeof(intel_wakeref_t) > sizeof(unsigned long));

	ret = intel_display_power_get(i915, POWER_DOMAIN_AUDIO_PLAYBACK);

	if (i915->display.audio.power_refcount++ == 0) {
		if (DISPLAY_VER(i915) >= 9) {
			intel_de_write(i915, AUD_FREQ_CNTRL,
				       i915->display.audio.freq_cntrl);
			drm_dbg_kms(&i915->drm,
				    "restored AUD_FREQ_CNTRL to 0x%x\n",
				    i915->display.audio.freq_cntrl);
		}

		/* Force CDCLK to 2*BCLK as long as we need audio powered. */
		if (IS_GEMINILAKE(i915))
			glk_force_audio_cdclk(i915, true);

		if (DISPLAY_VER(i915) >= 10)
			intel_de_rmw(i915, AUD_PIN_BUF_CTL,
				     0, AUD_PIN_BUF_ENABLE);
	}

	return ret;
}

static void i915_audio_component_put_power(struct device *kdev,
					   unsigned long cookie)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	/* Stop forcing CDCLK to 2*BCLK if no need for audio to be powered. */
	if (--i915->display.audio.power_refcount == 0)
		if (IS_GEMINILAKE(i915))
			glk_force_audio_cdclk(i915, false);

	intel_display_power_put(i915, POWER_DOMAIN_AUDIO_PLAYBACK, cookie);
}

static void i915_audio_component_codec_wake_override(struct device *kdev,
						     bool enable)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);
	unsigned long cookie;

	if (DISPLAY_VER(i915) < 9)
		return;

	cookie = i915_audio_component_get_power(kdev);

	/*
	 * Enable/disable generating the codec wake signal, overriding the
	 * internal logic to generate the codec wake to controller.
	 */
	intel_de_rmw(i915, HSW_AUD_CHICKENBIT,
		     SKL_AUD_CODEC_WAKE_SIGNAL, 0);
	usleep_range(1000, 1500);

	if (enable) {
		intel_de_rmw(i915, HSW_AUD_CHICKENBIT,
			     0, SKL_AUD_CODEC_WAKE_SIGNAL);
		usleep_range(1000, 1500);
	}

	i915_audio_component_put_power(kdev, cookie);
}

/* Get CDCLK in kHz  */
static int i915_audio_component_get_cdclk_freq(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (drm_WARN_ON_ONCE(&i915->drm, !HAS_DDI(i915)))
		return -ENODEV;

	return i915->display.cdclk.hw.cdclk;
}

/*
 * get the intel audio state according to the parameter port and cpu_transcoder
 * MST & (cpu_transcoder >= 0): return the audio.state[cpu_transcoder].encoder],
 *   when port is matched
 * MST & (cpu_transcoder < 0): this is invalid
 * Non-MST & (cpu_transcoder >= 0): only cpu_transcoder = 0 (the first device entry)
 *   will get the right intel_encoder with port matched
 * Non-MST & (cpu_transcoder < 0): get the right intel_encoder with port matched
 */
static struct intel_audio_state *find_audio_state(struct drm_i915_private *i915,
						  int port, int cpu_transcoder)
{
	/* MST */
	if (cpu_transcoder >= 0) {
		struct intel_audio_state *audio_state;
		struct intel_encoder *encoder;

		if (drm_WARN_ON(&i915->drm,
				cpu_transcoder >= ARRAY_SIZE(i915->display.audio.state)))
			return NULL;

		audio_state = &i915->display.audio.state[cpu_transcoder];
		encoder = audio_state->encoder;

		if (encoder && encoder->port == port &&
		    encoder->type == INTEL_OUTPUT_DP_MST)
			return audio_state;
	}

	/* Non-MST */
	if (cpu_transcoder > 0)
		return NULL;

	for_each_cpu_transcoder(i915, cpu_transcoder) {
		struct intel_audio_state *audio_state;
		struct intel_encoder *encoder;

		audio_state = &i915->display.audio.state[cpu_transcoder];
		encoder = audio_state->encoder;

		if (encoder && encoder->port == port &&
		    encoder->type != INTEL_OUTPUT_DP_MST)
			return audio_state;
	}

	return NULL;
}

static int i915_audio_component_sync_audio_rate(struct device *kdev, int port,
						int cpu_transcoder, int rate)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);
	struct i915_audio_component *acomp = i915->display.audio.component;
	const struct intel_audio_state *audio_state;
	struct intel_encoder *encoder;
	struct intel_crtc *crtc;
	unsigned long cookie;
	int err = 0;

	if (!HAS_DDI(i915))
		return 0;

	cookie = i915_audio_component_get_power(kdev);
	mutex_lock(&i915->display.audio.mutex);

	audio_state = find_audio_state(i915, port, cpu_transcoder);
	if (!audio_state) {
		drm_dbg_kms(&i915->drm, "Not valid for port %c\n", port_name(port));
		err = -ENODEV;
		goto unlock;
	}

	encoder = audio_state->encoder;

	/* FIXME stop using the legacy crtc pointer */
	crtc = to_intel_crtc(encoder->base.crtc);

	/* port must be valid now, otherwise the cpu_transcoder will be invalid */
	acomp->aud_sample_rate[port] = rate;

	/* FIXME get rid of the crtc->config stuff */
	hsw_audio_config_update(encoder, crtc->config);

 unlock:
	mutex_unlock(&i915->display.audio.mutex);
	i915_audio_component_put_power(kdev, cookie);
	return err;
}

static int i915_audio_component_get_eld(struct device *kdev, int port,
					int cpu_transcoder, bool *enabled,
					unsigned char *buf, int max_bytes)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);
	const struct intel_audio_state *audio_state;
	int ret = 0;

	mutex_lock(&i915->display.audio.mutex);

	audio_state = find_audio_state(i915, port, cpu_transcoder);
	if (!audio_state) {
		drm_dbg_kms(&i915->drm, "Not valid for port %c\n", port_name(port));
		mutex_unlock(&i915->display.audio.mutex);
		return -EINVAL;
	}

	*enabled = audio_state->encoder != NULL;
	if (*enabled) {
		const u8 *eld = audio_state->eld;

		ret = drm_eld_size(eld);
		memcpy(buf, eld, min(max_bytes, ret));
	}

	mutex_unlock(&i915->display.audio.mutex);
	return ret;
}

static const struct drm_audio_component_ops i915_audio_component_ops = {
	.owner		= THIS_MODULE,
	.get_power	= i915_audio_component_get_power,
	.put_power	= i915_audio_component_put_power,
	.codec_wake_override = i915_audio_component_codec_wake_override,
	.get_cdclk_freq	= i915_audio_component_get_cdclk_freq,
	.sync_audio_rate = i915_audio_component_sync_audio_rate,
	.get_eld	= i915_audio_component_get_eld,
};

static int i915_audio_component_bind(struct device *i915_kdev,
				     struct device *hda_kdev, void *data)
{
	struct i915_audio_component *acomp = data;
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	int i;

	if (drm_WARN_ON(&i915->drm, acomp->base.ops || acomp->base.dev))
		return -EEXIST;

	if (drm_WARN_ON(&i915->drm,
			!device_link_add(hda_kdev, i915_kdev,
					 DL_FLAG_STATELESS)))
		return -ENOMEM;

	drm_modeset_lock_all(&i915->drm);
	acomp->base.ops = &i915_audio_component_ops;
	acomp->base.dev = i915_kdev;
	BUILD_BUG_ON(MAX_PORTS != I915_MAX_PORTS);
	for (i = 0; i < ARRAY_SIZE(acomp->aud_sample_rate); i++)
		acomp->aud_sample_rate[i] = 0;
	i915->display.audio.component = acomp;
	drm_modeset_unlock_all(&i915->drm);

	return 0;
}

static void i915_audio_component_unbind(struct device *i915_kdev,
					struct device *hda_kdev, void *data)
{
	struct i915_audio_component *acomp = data;
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);

	drm_modeset_lock_all(&i915->drm);
	acomp->base.ops = NULL;
	acomp->base.dev = NULL;
	i915->display.audio.component = NULL;
	drm_modeset_unlock_all(&i915->drm);

	device_link_remove(hda_kdev, i915_kdev);

	if (i915->display.audio.power_refcount)
		drm_err(&i915->drm, "audio power refcount %d after unbind\n",
			i915->display.audio.power_refcount);
}

static const struct component_ops i915_audio_component_bind_ops = {
	.bind	= i915_audio_component_bind,
	.unbind	= i915_audio_component_unbind,
};

#define AUD_FREQ_TMODE_SHIFT	14
#define AUD_FREQ_4T		0
#define AUD_FREQ_8T		(2 << AUD_FREQ_TMODE_SHIFT)
#define AUD_FREQ_PULLCLKS(x)	(((x) & 0x3) << 11)
#define AUD_FREQ_BCLK_96M	BIT(4)

#define AUD_FREQ_GEN12          (AUD_FREQ_8T | AUD_FREQ_PULLCLKS(0) | AUD_FREQ_BCLK_96M)
#define AUD_FREQ_TGL_BROKEN     (AUD_FREQ_8T | AUD_FREQ_PULLCLKS(2) | AUD_FREQ_BCLK_96M)

/**
 * i915_audio_component_init - initialize and register the audio component
 * @i915: i915 device instance
 *
 * This will register with the component framework a child component which
 * will bind dynamically to the snd_hda_intel driver's corresponding master
 * component when the latter is registered. During binding the child
 * initializes an instance of struct i915_audio_component which it receives
 * from the master. The master can then start to use the interface defined by
 * this struct. Each side can break the binding at any point by deregistering
 * its own component after which each side's component unbind callback is
 * called.
 *
 * We ignore any error during registration and continue with reduced
 * functionality (i.e. without HDMI audio).
 */
static void i915_audio_component_init(struct drm_i915_private *i915)
{
	u32 aud_freq, aud_freq_init;
	int ret;

	ret = component_add_typed(i915->drm.dev,
				  &i915_audio_component_bind_ops,
				  I915_COMPONENT_AUDIO);
	if (ret < 0) {
		drm_err(&i915->drm,
			"failed to add audio component (%d)\n", ret);
		/* continue with reduced functionality */
		return;
	}

	if (DISPLAY_VER(i915) >= 9) {
		aud_freq_init = intel_de_read(i915, AUD_FREQ_CNTRL);

		if (DISPLAY_VER(i915) >= 12)
			aud_freq = AUD_FREQ_GEN12;
		else
			aud_freq = aud_freq_init;

		/* use BIOS provided value for TGL and RKL unless it is a known bad value */
		if ((IS_TIGERLAKE(i915) || IS_ROCKETLAKE(i915)) &&
		    aud_freq_init != AUD_FREQ_TGL_BROKEN)
			aud_freq = aud_freq_init;

		drm_dbg_kms(&i915->drm, "use AUD_FREQ_CNTRL of 0x%x (init value 0x%x)\n",
			    aud_freq, aud_freq_init);

		i915->display.audio.freq_cntrl = aud_freq;
	}

	/* init with current cdclk */
	intel_audio_cdclk_change_post(i915);

	i915->display.audio.component_registered = true;
}

/**
 * i915_audio_component_cleanup - deregister the audio component
 * @i915: i915 device instance
 *
 * Deregisters the audio component, breaking any existing binding to the
 * corresponding snd_hda_intel driver's master component.
 */
static void i915_audio_component_cleanup(struct drm_i915_private *i915)
{
	if (!i915->display.audio.component_registered)
		return;

	component_del(i915->drm.dev, &i915_audio_component_bind_ops);
	i915->display.audio.component_registered = false;
}

/**
 * intel_audio_init() - Initialize the audio driver either using
 * component framework or using lpe audio bridge
 * @i915: the i915 drm device private data
 *
 */
void intel_audio_init(struct drm_i915_private *i915)
{
	if (intel_lpe_audio_init(i915) < 0)
		i915_audio_component_init(i915);
}

/**
 * intel_audio_deinit() - deinitialize the audio driver
 * @i915: the i915 drm device private data
 *
 */
void intel_audio_deinit(struct drm_i915_private *i915)
{
	if (i915->display.audio.lpe.platdev != NULL)
		intel_lpe_audio_teardown(i915);
	else
		i915_audio_component_cleanup(i915);
}

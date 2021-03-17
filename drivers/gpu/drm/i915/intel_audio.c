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

#include <linux/kernel.h>
#include <linux/component.h>
#include <drm/i915_component.h>
#include <drm/intel_lpe_audio.h>
#include "intel_drv.h"

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include "i915_drv.h"

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
};

/* HDMI N/CTS table */
#define TMDS_297M 297000
#define TMDS_296M 296703
static const struct {
	int sample_rate;
	int clock;
	int n;
	int cts;
} hdmi_aud_ncts[] = {
	{ 44100, TMDS_296M, 4459, 234375 },
	{ 44100, TMDS_297M, 4704, 247500 },
	{ 48000, TMDS_296M, 5824, 281250 },
	{ 48000, TMDS_297M, 5120, 247500 },
	{ 32000, TMDS_296M, 5824, 421875 },
	{ 32000, TMDS_297M, 3072, 222750 },
	{ 88200, TMDS_296M, 8918, 234375 },
	{ 88200, TMDS_297M, 9408, 247500 },
	{ 96000, TMDS_296M, 11648, 281250 },
	{ 96000, TMDS_297M, 10240, 247500 },
	{ 176400, TMDS_296M, 17836, 234375 },
	{ 176400, TMDS_297M, 18816, 247500 },
	{ 192000, TMDS_296M, 23296, 281250 },
	{ 192000, TMDS_297M, 20480, 247500 },
};

/* get AUD_CONFIG_PIXEL_CLOCK_HDMI_* value for mode */
static u32 audio_config_hdmi_pixel_clock(const struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;
	int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_audio_clock); i++) {
		if (adjusted_mode->crtc_clock == hdmi_audio_clock[i].clock)
			break;
	}

	if (i == ARRAY_SIZE(hdmi_audio_clock)) {
		DRM_DEBUG_KMS("HDMI audio pixel clock setting for %d not found, falling back to defaults\n",
			      adjusted_mode->crtc_clock);
		i = 1;
	}

	DRM_DEBUG_KMS("Configuring HDMI audio for pixel clock %d (0x%08x)\n",
		      hdmi_audio_clock[i].clock,
		      hdmi_audio_clock[i].config);

	return hdmi_audio_clock[i].config;
}

static int audio_config_hdmi_get_n(const struct intel_crtc_state *crtc_state,
				   int rate)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;
	int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_aud_ncts); i++) {
		if (rate == hdmi_aud_ncts[i].sample_rate &&
		    adjusted_mode->crtc_clock == hdmi_aud_ncts[i].clock) {
			return hdmi_aud_ncts[i].n;
		}
	}
	return 0;
}

static bool intel_eld_uptodate(struct drm_connector *connector,
			       i915_reg_t reg_eldv, u32 bits_eldv,
			       i915_reg_t reg_elda, u32 bits_elda,
			       i915_reg_t reg_edid)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	const u8 *eld = connector->eld;
	u32 tmp;
	int i;

	tmp = I915_READ(reg_eldv);
	tmp &= bits_eldv;

	if (!tmp)
		return false;

	tmp = I915_READ(reg_elda);
	tmp &= ~bits_elda;
	I915_WRITE(reg_elda, tmp);

	for (i = 0; i < drm_eld_size(eld) / 4; i++)
		if (I915_READ(reg_edid) != *((const u32 *)eld + i))
			return false;

	return true;
}

static void g4x_audio_codec_disable(struct intel_encoder *encoder,
				    const struct intel_crtc_state *old_crtc_state,
				    const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 eldv, tmp;

	DRM_DEBUG_KMS("Disable audio codec\n");

	tmp = I915_READ(G4X_AUD_VID_DID);
	if (tmp == INTEL_AUDIO_DEVBLC || tmp == INTEL_AUDIO_DEVCL)
		eldv = G4X_ELDV_DEVCL_DEVBLC;
	else
		eldv = G4X_ELDV_DEVCTG;

	/* Invalidate ELD */
	tmp = I915_READ(G4X_AUD_CNTL_ST);
	tmp &= ~eldv;
	I915_WRITE(G4X_AUD_CNTL_ST, tmp);
}

static void g4x_audio_codec_enable(struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_connector *connector = conn_state->connector;
	const u8 *eld = connector->eld;
	u32 eldv;
	u32 tmp;
	int len, i;

	DRM_DEBUG_KMS("Enable audio codec, %u bytes ELD\n", drm_eld_size(eld));

	tmp = I915_READ(G4X_AUD_VID_DID);
	if (tmp == INTEL_AUDIO_DEVBLC || tmp == INTEL_AUDIO_DEVCL)
		eldv = G4X_ELDV_DEVCL_DEVBLC;
	else
		eldv = G4X_ELDV_DEVCTG;

	if (intel_eld_uptodate(connector,
			       G4X_AUD_CNTL_ST, eldv,
			       G4X_AUD_CNTL_ST, G4X_ELD_ADDR_MASK,
			       G4X_HDMIW_HDMIEDID))
		return;

	tmp = I915_READ(G4X_AUD_CNTL_ST);
	tmp &= ~(eldv | G4X_ELD_ADDR_MASK);
	len = (tmp >> 9) & 0x1f;		/* ELD buffer size */
	I915_WRITE(G4X_AUD_CNTL_ST, tmp);

	len = min(drm_eld_size(eld) / 4, len);
	DRM_DEBUG_DRIVER("ELD size %d\n", len);
	for (i = 0; i < len; i++)
		I915_WRITE(G4X_HDMIW_HDMIEDID, *((const u32 *)eld + i));

	tmp = I915_READ(G4X_AUD_CNTL_ST);
	tmp |= eldv;
	I915_WRITE(G4X_AUD_CNTL_ST, tmp);
}

static void
hsw_dp_audio_config_update(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct i915_audio_component *acomp = dev_priv->audio_component;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	enum port port = encoder->port;
	enum pipe pipe = crtc->pipe;
	const struct dp_aud_n_m *nm;
	int rate;
	u32 tmp;

	rate = acomp ? acomp->aud_sample_rate[port] : 0;
	nm = audio_config_dp_get_n_m(crtc_state, rate);
	if (nm)
		DRM_DEBUG_KMS("using Maud %u, Naud %u\n", nm->m, nm->n);
	else
		DRM_DEBUG_KMS("using automatic Maud, Naud\n");

	tmp = I915_READ(HSW_AUD_CFG(pipe));
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp &= ~AUD_CONFIG_PIXEL_CLOCK_HDMI_MASK;
	tmp &= ~AUD_CONFIG_N_PROG_ENABLE;
	tmp |= AUD_CONFIG_N_VALUE_INDEX;

	if (nm) {
		tmp &= ~AUD_CONFIG_N_MASK;
		tmp |= AUD_CONFIG_N(nm->n);
		tmp |= AUD_CONFIG_N_PROG_ENABLE;
	}

	I915_WRITE(HSW_AUD_CFG(pipe), tmp);

	tmp = I915_READ(HSW_AUD_M_CTS_ENABLE(pipe));
	tmp &= ~AUD_CONFIG_M_MASK;
	tmp &= ~AUD_M_CTS_M_VALUE_INDEX;
	tmp &= ~AUD_M_CTS_M_PROG_ENABLE;

	if (nm) {
		tmp |= nm->m;
		tmp |= AUD_M_CTS_M_VALUE_INDEX;
		tmp |= AUD_M_CTS_M_PROG_ENABLE;
	}

	I915_WRITE(HSW_AUD_M_CTS_ENABLE(pipe), tmp);
}

static void
hsw_hdmi_audio_config_update(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct i915_audio_component *acomp = dev_priv->audio_component;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	enum port port = encoder->port;
	enum pipe pipe = crtc->pipe;
	int n, rate;
	u32 tmp;

	rate = acomp ? acomp->aud_sample_rate[port] : 0;

	tmp = I915_READ(HSW_AUD_CFG(pipe));
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp &= ~AUD_CONFIG_PIXEL_CLOCK_HDMI_MASK;
	tmp &= ~AUD_CONFIG_N_PROG_ENABLE;
	tmp |= audio_config_hdmi_pixel_clock(crtc_state);

	n = audio_config_hdmi_get_n(crtc_state, rate);
	if (n != 0) {
		DRM_DEBUG_KMS("using N %d\n", n);

		tmp &= ~AUD_CONFIG_N_MASK;
		tmp |= AUD_CONFIG_N(n);
		tmp |= AUD_CONFIG_N_PROG_ENABLE;
	} else {
		DRM_DEBUG_KMS("using automatic N\n");
	}

	I915_WRITE(HSW_AUD_CFG(pipe), tmp);

	/*
	 * Let's disable "Enable CTS or M Prog bit"
	 * and let HW calculate the value
	 */
	tmp = I915_READ(HSW_AUD_M_CTS_ENABLE(pipe));
	tmp &= ~AUD_M_CTS_M_PROG_ENABLE;
	tmp &= ~AUD_M_CTS_M_VALUE_INDEX;
	I915_WRITE(HSW_AUD_M_CTS_ENABLE(pipe), tmp);
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
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->base.crtc);
	enum pipe pipe = crtc->pipe;
	u32 tmp;

	DRM_DEBUG_KMS("Disable audio codec on pipe %c\n", pipe_name(pipe));

	mutex_lock(&dev_priv->av_mutex);

	/* Disable timestamps */
	tmp = I915_READ(HSW_AUD_CFG(pipe));
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp |= AUD_CONFIG_N_PROG_ENABLE;
	tmp &= ~AUD_CONFIG_UPPER_N_MASK;
	tmp &= ~AUD_CONFIG_LOWER_N_MASK;
	if (intel_crtc_has_dp_encoder(old_crtc_state))
		tmp |= AUD_CONFIG_N_VALUE_INDEX;
	I915_WRITE(HSW_AUD_CFG(pipe), tmp);

	/* Invalidate ELD */
	tmp = I915_READ(HSW_AUD_PIN_ELD_CP_VLD);
	tmp &= ~AUDIO_ELD_VALID(pipe);
	tmp &= ~AUDIO_OUTPUT_ENABLE(pipe);
	I915_WRITE(HSW_AUD_PIN_ELD_CP_VLD, tmp);

	mutex_unlock(&dev_priv->av_mutex);
}

static void hsw_audio_codec_enable(struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_connector *connector = conn_state->connector;
	enum pipe pipe = crtc->pipe;
	const u8 *eld = connector->eld;
	u32 tmp;
	int len, i;

	DRM_DEBUG_KMS("Enable audio codec on pipe %c, %u bytes ELD\n",
		      pipe_name(pipe), drm_eld_size(eld));

	mutex_lock(&dev_priv->av_mutex);

	/* Enable audio presence detect, invalidate ELD */
	tmp = I915_READ(HSW_AUD_PIN_ELD_CP_VLD);
	tmp |= AUDIO_OUTPUT_ENABLE(pipe);
	tmp &= ~AUDIO_ELD_VALID(pipe);
	I915_WRITE(HSW_AUD_PIN_ELD_CP_VLD, tmp);

	/*
	 * FIXME: We're supposed to wait for vblank here, but we have vblanks
	 * disabled during the mode set. The proper fix would be to push the
	 * rest of the setup into a vblank work item, queued here, but the
	 * infrastructure is not there yet.
	 */

	/* Reset ELD write address */
	tmp = I915_READ(HSW_AUD_DIP_ELD_CTRL(pipe));
	tmp &= ~IBX_ELD_ADDRESS_MASK;
	I915_WRITE(HSW_AUD_DIP_ELD_CTRL(pipe), tmp);

	/* Up to 84 bytes of hw ELD buffer */
	len = min(drm_eld_size(eld), 84);
	for (i = 0; i < len / 4; i++)
		I915_WRITE(HSW_AUD_EDID_DATA(pipe), *((const u32 *)eld + i));

	/* ELD valid */
	tmp = I915_READ(HSW_AUD_PIN_ELD_CP_VLD);
	tmp |= AUDIO_ELD_VALID(pipe);
	I915_WRITE(HSW_AUD_PIN_ELD_CP_VLD, tmp);

	/* Enable timestamps */
	hsw_audio_config_update(encoder, crtc_state);

	mutex_unlock(&dev_priv->av_mutex);
}

static void ilk_audio_codec_disable(struct intel_encoder *encoder,
				    const struct intel_crtc_state *old_crtc_state,
				    const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->base.crtc);
	enum pipe pipe = crtc->pipe;
	enum port port = encoder->port;
	u32 tmp, eldv;
	i915_reg_t aud_config, aud_cntrl_st2;

	DRM_DEBUG_KMS("Disable audio codec on port %c, pipe %c\n",
		      port_name(port), pipe_name(pipe));

	if (WARN_ON(port == PORT_A))
		return;

	if (HAS_PCH_IBX(dev_priv)) {
		aud_config = IBX_AUD_CFG(pipe);
		aud_cntrl_st2 = IBX_AUD_CNTL_ST2;
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		aud_config = VLV_AUD_CFG(pipe);
		aud_cntrl_st2 = VLV_AUD_CNTL_ST2;
	} else {
		aud_config = CPT_AUD_CFG(pipe);
		aud_cntrl_st2 = CPT_AUD_CNTRL_ST2;
	}

	/* Disable timestamps */
	tmp = I915_READ(aud_config);
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp |= AUD_CONFIG_N_PROG_ENABLE;
	tmp &= ~AUD_CONFIG_UPPER_N_MASK;
	tmp &= ~AUD_CONFIG_LOWER_N_MASK;
	if (intel_crtc_has_dp_encoder(old_crtc_state))
		tmp |= AUD_CONFIG_N_VALUE_INDEX;
	I915_WRITE(aud_config, tmp);

	eldv = IBX_ELD_VALID(port);

	/* Invalidate ELD */
	tmp = I915_READ(aud_cntrl_st2);
	tmp &= ~eldv;
	I915_WRITE(aud_cntrl_st2, tmp);
}

static void ilk_audio_codec_enable(struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state,
				   const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_connector *connector = conn_state->connector;
	enum pipe pipe = crtc->pipe;
	enum port port = encoder->port;
	const u8 *eld = connector->eld;
	u32 tmp, eldv;
	int len, i;
	i915_reg_t hdmiw_hdmiedid, aud_config, aud_cntl_st, aud_cntrl_st2;

	DRM_DEBUG_KMS("Enable audio codec on port %c, pipe %c, %u bytes ELD\n",
		      port_name(port), pipe_name(pipe), drm_eld_size(eld));

	if (WARN_ON(port == PORT_A))
		return;

	/*
	 * FIXME: We're supposed to wait for vblank here, but we have vblanks
	 * disabled during the mode set. The proper fix would be to push the
	 * rest of the setup into a vblank work item, queued here, but the
	 * infrastructure is not there yet.
	 */

	if (HAS_PCH_IBX(dev_priv)) {
		hdmiw_hdmiedid = IBX_HDMIW_HDMIEDID(pipe);
		aud_config = IBX_AUD_CFG(pipe);
		aud_cntl_st = IBX_AUD_CNTL_ST(pipe);
		aud_cntrl_st2 = IBX_AUD_CNTL_ST2;
	} else if (IS_VALLEYVIEW(dev_priv) ||
		   IS_CHERRYVIEW(dev_priv)) {
		hdmiw_hdmiedid = VLV_HDMIW_HDMIEDID(pipe);
		aud_config = VLV_AUD_CFG(pipe);
		aud_cntl_st = VLV_AUD_CNTL_ST(pipe);
		aud_cntrl_st2 = VLV_AUD_CNTL_ST2;
	} else {
		hdmiw_hdmiedid = CPT_HDMIW_HDMIEDID(pipe);
		aud_config = CPT_AUD_CFG(pipe);
		aud_cntl_st = CPT_AUD_CNTL_ST(pipe);
		aud_cntrl_st2 = CPT_AUD_CNTRL_ST2;
	}

	eldv = IBX_ELD_VALID(port);

	/* Invalidate ELD */
	tmp = I915_READ(aud_cntrl_st2);
	tmp &= ~eldv;
	I915_WRITE(aud_cntrl_st2, tmp);

	/* Reset ELD write address */
	tmp = I915_READ(aud_cntl_st);
	tmp &= ~IBX_ELD_ADDRESS_MASK;
	I915_WRITE(aud_cntl_st, tmp);

	/* Up to 84 bytes of hw ELD buffer */
	len = min(drm_eld_size(eld), 84);
	for (i = 0; i < len / 4; i++)
		I915_WRITE(hdmiw_hdmiedid, *((const u32 *)eld + i));

	/* ELD valid */
	tmp = I915_READ(aud_cntrl_st2);
	tmp |= eldv;
	I915_WRITE(aud_cntrl_st2, tmp);

	/* Enable timestamps */
	tmp = I915_READ(aud_config);
	tmp &= ~AUD_CONFIG_N_VALUE_INDEX;
	tmp &= ~AUD_CONFIG_N_PROG_ENABLE;
	tmp &= ~AUD_CONFIG_PIXEL_CLOCK_HDMI_MASK;
	if (intel_crtc_has_dp_encoder(crtc_state))
		tmp |= AUD_CONFIG_N_VALUE_INDEX;
	else
		tmp |= audio_config_hdmi_pixel_clock(crtc_state);
	I915_WRITE(aud_config, tmp);
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
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct i915_audio_component *acomp = dev_priv->audio_component;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_connector *connector = conn_state->connector;
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;
	enum port port = encoder->port;
	enum pipe pipe = crtc->pipe;

	if (!connector->eld[0])
		return;

	DRM_DEBUG_DRIVER("ELD on [CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
			 connector->base.id,
			 connector->name,
			 connector->encoder->base.id,
			 connector->encoder->name);

	connector->eld[6] = drm_av_sync_delay(connector, adjusted_mode) / 2;

	if (dev_priv->display.audio_codec_enable)
		dev_priv->display.audio_codec_enable(encoder,
						     crtc_state,
						     conn_state);

	mutex_lock(&dev_priv->av_mutex);
	encoder->audio_connector = connector;

	/* referred in audio callbacks */
	dev_priv->av_enc_map[pipe] = encoder;
	mutex_unlock(&dev_priv->av_mutex);

	if (acomp && acomp->base.audio_ops &&
	    acomp->base.audio_ops->pin_eld_notify) {
		/* audio drivers expect pipe = -1 to indicate Non-MST cases */
		if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST))
			pipe = -1;
		acomp->base.audio_ops->pin_eld_notify(acomp->base.audio_ops->audio_ptr,
						 (int) port, (int) pipe);
	}

	intel_lpe_audio_notify(dev_priv, pipe, port, connector->eld,
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
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct i915_audio_component *acomp = dev_priv->audio_component;
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->base.crtc);
	enum port port = encoder->port;
	enum pipe pipe = crtc->pipe;

	if (dev_priv->display.audio_codec_disable)
		dev_priv->display.audio_codec_disable(encoder,
						      old_crtc_state,
						      old_conn_state);

	mutex_lock(&dev_priv->av_mutex);
	encoder->audio_connector = NULL;
	dev_priv->av_enc_map[pipe] = NULL;
	mutex_unlock(&dev_priv->av_mutex);

	if (acomp && acomp->base.audio_ops &&
	    acomp->base.audio_ops->pin_eld_notify) {
		/* audio drivers expect pipe = -1 to indicate Non-MST cases */
		if (!intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_DP_MST))
			pipe = -1;
		acomp->base.audio_ops->pin_eld_notify(acomp->base.audio_ops->audio_ptr,
						 (int) port, (int) pipe);
	}

	intel_lpe_audio_notify(dev_priv, pipe, port, NULL, 0, false);
}

/**
 * intel_init_audio_hooks - Set up chip specific audio hooks
 * @dev_priv: device private
 */
void intel_init_audio_hooks(struct drm_i915_private *dev_priv)
{
	if (IS_G4X(dev_priv)) {
		dev_priv->display.audio_codec_enable = g4x_audio_codec_enable;
		dev_priv->display.audio_codec_disable = g4x_audio_codec_disable;
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		dev_priv->display.audio_codec_enable = ilk_audio_codec_enable;
		dev_priv->display.audio_codec_disable = ilk_audio_codec_disable;
	} else if (IS_HASWELL(dev_priv) || INTEL_GEN(dev_priv) >= 8) {
		dev_priv->display.audio_codec_enable = hsw_audio_codec_enable;
		dev_priv->display.audio_codec_disable = hsw_audio_codec_disable;
	} else if (HAS_PCH_SPLIT(dev_priv)) {
		dev_priv->display.audio_codec_enable = ilk_audio_codec_enable;
		dev_priv->display.audio_codec_disable = ilk_audio_codec_disable;
	}
}

static void i915_audio_component_get_power(struct device *kdev)
{
	intel_display_power_get(kdev_to_i915(kdev), POWER_DOMAIN_AUDIO);
}

static void i915_audio_component_put_power(struct device *kdev)
{
	intel_display_power_put(kdev_to_i915(kdev), POWER_DOMAIN_AUDIO);
}

static void i915_audio_component_codec_wake_override(struct device *kdev,
						     bool enable)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(kdev);
	u32 tmp;

	if (!IS_GEN9(dev_priv))
		return;

	i915_audio_component_get_power(kdev);

	/*
	 * Enable/disable generating the codec wake signal, overriding the
	 * internal logic to generate the codec wake to controller.
	 */
	tmp = I915_READ(HSW_AUD_CHICKENBIT);
	tmp &= ~SKL_AUD_CODEC_WAKE_SIGNAL;
	I915_WRITE(HSW_AUD_CHICKENBIT, tmp);
	usleep_range(1000, 1500);

	if (enable) {
		tmp = I915_READ(HSW_AUD_CHICKENBIT);
		tmp |= SKL_AUD_CODEC_WAKE_SIGNAL;
		I915_WRITE(HSW_AUD_CHICKENBIT, tmp);
		usleep_range(1000, 1500);
	}

	i915_audio_component_put_power(kdev);
}

/* Get CDCLK in kHz  */
static int i915_audio_component_get_cdclk_freq(struct device *kdev)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(kdev);

	if (WARN_ON_ONCE(!HAS_DDI(dev_priv)))
		return -ENODEV;

	return dev_priv->cdclk.hw.cdclk;
}

/*
 * get the intel_encoder according to the parameter port and pipe
 * intel_encoder is saved by the index of pipe
 * MST & (pipe >= 0): return the av_enc_map[pipe],
 *   when port is matched
 * MST & (pipe < 0): this is invalid
 * Non-MST & (pipe >= 0): only pipe = 0 (the first device entry)
 *   will get the right intel_encoder with port matched
 * Non-MST & (pipe < 0): get the right intel_encoder with port matched
 */
static struct intel_encoder *get_saved_enc(struct drm_i915_private *dev_priv,
					       int port, int pipe)
{
	struct intel_encoder *encoder;

	/* MST */
	if (pipe >= 0) {
		if (WARN_ON(pipe >= ARRAY_SIZE(dev_priv->av_enc_map)))
			return NULL;

		encoder = dev_priv->av_enc_map[pipe];
		/*
		 * when bootup, audio driver may not know it is
		 * MST or not. So it will poll all the port & pipe
		 * combinations
		 */
		if (encoder != NULL && encoder->port == port &&
		    encoder->type == INTEL_OUTPUT_DP_MST)
			return encoder;
	}

	/* Non-MST */
	if (pipe > 0)
		return NULL;

	for_each_pipe(dev_priv, pipe) {
		encoder = dev_priv->av_enc_map[pipe];
		if (encoder == NULL)
			continue;

		if (encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (port == encoder->port)
			return encoder;
	}

	return NULL;
}

static int i915_audio_component_sync_audio_rate(struct device *kdev, int port,
						int pipe, int rate)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(kdev);
	struct i915_audio_component *acomp = dev_priv->audio_component;
	struct intel_encoder *encoder;
	struct intel_crtc *crtc;
	int err = 0;

	if (!HAS_DDI(dev_priv))
		return 0;

	i915_audio_component_get_power(kdev);
	mutex_lock(&dev_priv->av_mutex);

	/* 1. get the pipe */
	encoder = get_saved_enc(dev_priv, port, pipe);
	if (!encoder || !encoder->base.crtc) {
		DRM_DEBUG_KMS("Not valid for port %c\n", port_name(port));
		err = -ENODEV;
		goto unlock;
	}

	crtc = to_intel_crtc(encoder->base.crtc);

	/* port must be valid now, otherwise the pipe will be invalid */
	acomp->aud_sample_rate[port] = rate;

	hsw_audio_config_update(encoder, crtc->config);

 unlock:
	mutex_unlock(&dev_priv->av_mutex);
	i915_audio_component_put_power(kdev);
	return err;
}

static int i915_audio_component_get_eld(struct device *kdev, int port,
					int pipe, bool *enabled,
					unsigned char *buf, int max_bytes)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(kdev);
	struct intel_encoder *intel_encoder;
	const u8 *eld;
	int ret = -EINVAL;

	mutex_lock(&dev_priv->av_mutex);

	intel_encoder = get_saved_enc(dev_priv, port, pipe);
	if (!intel_encoder) {
		DRM_DEBUG_KMS("Not valid for port %c\n", port_name(port));
		mutex_unlock(&dev_priv->av_mutex);
		return ret;
	}

	ret = 0;
	*enabled = intel_encoder->audio_connector != NULL;
	if (*enabled) {
		eld = intel_encoder->audio_connector->eld;
		ret = drm_eld_size(eld);
		memcpy(buf, eld, min(max_bytes, ret));
	}

	mutex_unlock(&dev_priv->av_mutex);
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
	struct drm_i915_private *dev_priv = kdev_to_i915(i915_kdev);
	int i;

	if (WARN_ON(acomp->base.ops || acomp->base.dev))
		return -EEXIST;

	drm_modeset_lock_all(&dev_priv->drm);
	acomp->base.ops = &i915_audio_component_ops;
	acomp->base.dev = i915_kdev;
	BUILD_BUG_ON(MAX_PORTS != I915_MAX_PORTS);
	for (i = 0; i < ARRAY_SIZE(acomp->aud_sample_rate); i++)
		acomp->aud_sample_rate[i] = 0;
	dev_priv->audio_component = acomp;
	drm_modeset_unlock_all(&dev_priv->drm);

	return 0;
}

static void i915_audio_component_unbind(struct device *i915_kdev,
					struct device *hda_kdev, void *data)
{
	struct i915_audio_component *acomp = data;
	struct drm_i915_private *dev_priv = kdev_to_i915(i915_kdev);

	drm_modeset_lock_all(&dev_priv->drm);
	acomp->base.ops = NULL;
	acomp->base.dev = NULL;
	dev_priv->audio_component = NULL;
	drm_modeset_unlock_all(&dev_priv->drm);
}

static const struct component_ops i915_audio_component_bind_ops = {
	.bind	= i915_audio_component_bind,
	.unbind	= i915_audio_component_unbind,
};

/**
 * i915_audio_component_init - initialize and register the audio component
 * @dev_priv: i915 device instance
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
void i915_audio_component_init(struct drm_i915_private *dev_priv)
{
	int ret;

	ret = component_add(dev_priv->drm.dev, &i915_audio_component_bind_ops);
	if (ret < 0) {
		DRM_ERROR("failed to add audio component (%d)\n", ret);
		/* continue with reduced functionality */
		return;
	}

	dev_priv->audio_component_registered = true;
}

/**
 * i915_audio_component_cleanup - deregister the audio component
 * @dev_priv: i915 device instance
 *
 * Deregisters the audio component, breaking any existing binding to the
 * corresponding snd_hda_intel driver's master component.
 */
void i915_audio_component_cleanup(struct drm_i915_private *dev_priv)
{
	if (!dev_priv->audio_component_registered)
		return;

	component_del(dev_priv->drm.dev, &i915_audio_component_bind_ops);
	dev_priv->audio_component_registered = false;
}

/**
 * intel_audio_init() - Initialize the audio driver either using
 * component framework or using lpe audio bridge
 * @dev_priv: the i915 drm device private data
 *
 */
void intel_audio_init(struct drm_i915_private *dev_priv)
{
	if (intel_lpe_audio_init(dev_priv) < 0)
		i915_audio_component_init(dev_priv);
}

/**
 * intel_audio_deinit() - deinitialize the audio driver
 * @dev_priv: the i915 drm device private data
 *
 */
void intel_audio_deinit(struct drm_i915_private *dev_priv)
{
	if ((dev_priv)->lpe_audio.platdev != NULL)
		intel_lpe_audio_teardown(dev_priv);
	else
		i915_audio_component_cleanup(dev_priv);
}

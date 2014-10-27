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

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include "intel_drv.h"
#include "i915_drv.h"

static const struct {
	int clock;
	u32 config;
} hdmi_audio_clock[] = {
	{ DIV_ROUND_UP(25200 * 1000, 1001), AUD_CONFIG_PIXEL_CLOCK_HDMI_25175 },
	{ 25200, AUD_CONFIG_PIXEL_CLOCK_HDMI_25200 }, /* default per bspec */
	{ 27000, AUD_CONFIG_PIXEL_CLOCK_HDMI_27000 },
	{ 27000 * 1001 / 1000, AUD_CONFIG_PIXEL_CLOCK_HDMI_27027 },
	{ 54000, AUD_CONFIG_PIXEL_CLOCK_HDMI_54000 },
	{ 54000 * 1001 / 1000, AUD_CONFIG_PIXEL_CLOCK_HDMI_54054 },
	{ DIV_ROUND_UP(74250 * 1000, 1001), AUD_CONFIG_PIXEL_CLOCK_HDMI_74176 },
	{ 74250, AUD_CONFIG_PIXEL_CLOCK_HDMI_74250 },
	{ DIV_ROUND_UP(148500 * 1000, 1001), AUD_CONFIG_PIXEL_CLOCK_HDMI_148352 },
	{ 148500, AUD_CONFIG_PIXEL_CLOCK_HDMI_148500 },
};

/* get AUD_CONFIG_PIXEL_CLOCK_HDMI_* value for mode */
static u32 audio_config_hdmi_pixel_clock(struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_audio_clock); i++) {
		if (mode->clock == hdmi_audio_clock[i].clock)
			break;
	}

	if (i == ARRAY_SIZE(hdmi_audio_clock)) {
		DRM_DEBUG_KMS("HDMI audio pixel clock setting for %d not found, falling back to defaults\n", mode->clock);
		i = 1;
	}

	DRM_DEBUG_KMS("Configuring HDMI audio for pixel clock %d (0x%08x)\n",
		      hdmi_audio_clock[i].clock,
		      hdmi_audio_clock[i].config);

	return hdmi_audio_clock[i].config;
}

static bool intel_eld_uptodate(struct drm_connector *connector,
			       int reg_eldv, uint32_t bits_eldv,
			       int reg_elda, uint32_t bits_elda,
			       int reg_edid)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	uint8_t *eld = connector->eld;
	uint32_t tmp;
	int i;

	tmp = I915_READ(reg_eldv);
	tmp &= bits_eldv;

	if (!eld[0])
		return !tmp;

	if (!tmp)
		return false;

	tmp = I915_READ(reg_elda);
	tmp &= ~bits_elda;
	I915_WRITE(reg_elda, tmp);

	for (i = 0; i < eld[2]; i++)
		if (I915_READ(reg_edid) != *((uint32_t *)eld + i))
			return false;

	return true;
}

static void g4x_write_eld(struct drm_connector *connector,
			  struct intel_encoder *encoder,
			  struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	uint8_t *eld = connector->eld;
	uint32_t eldv;
	uint32_t tmp;
	int len, i;

	tmp = I915_READ(G4X_AUD_VID_DID);
	if (tmp == INTEL_AUDIO_DEVBLC || tmp == INTEL_AUDIO_DEVCL)
		eldv = G4X_ELDV_DEVCL_DEVBLC;
	else
		eldv = G4X_ELDV_DEVCTG;

	if (intel_eld_uptodate(connector,
			       G4X_AUD_CNTL_ST, eldv,
			       G4X_AUD_CNTL_ST, G4X_ELD_ADDR,
			       G4X_HDMIW_HDMIEDID))
		return;

	tmp = I915_READ(G4X_AUD_CNTL_ST);
	tmp &= ~(eldv | G4X_ELD_ADDR);
	len = (tmp >> 9) & 0x1f;		/* ELD buffer size */
	I915_WRITE(G4X_AUD_CNTL_ST, tmp);

	if (!eld[0])
		return;

	len = min_t(int, eld[2], len);
	DRM_DEBUG_DRIVER("ELD size %d\n", len);
	for (i = 0; i < len; i++)
		I915_WRITE(G4X_HDMIW_HDMIEDID, *((uint32_t *)eld + i));

	tmp = I915_READ(G4X_AUD_CNTL_ST);
	tmp |= eldv;
	I915_WRITE(G4X_AUD_CNTL_ST, tmp);
}

static void haswell_write_eld(struct drm_connector *connector,
			      struct intel_encoder *encoder,
			      struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	uint8_t *eld = connector->eld;
	uint32_t eldv;
	uint32_t tmp;
	int len, i;
	enum pipe pipe = intel_crtc->pipe;
	enum port port;
	int hdmiw_hdmiedid = HSW_AUD_EDID_DATA(pipe);
	int aud_cntl_st = HSW_AUD_DIP_ELD_CTRL(pipe);
	int aud_config = HSW_AUD_CFG(pipe);
	int aud_cntrl_st2 = HSW_AUD_PIN_ELD_CP_VLD;

	/* Audio output enable */
	DRM_DEBUG_DRIVER("HDMI audio: enable codec\n");
	tmp = I915_READ(aud_cntrl_st2);
	tmp |= (AUDIO_OUTPUT_ENABLE_A << (pipe * 4));
	I915_WRITE(aud_cntrl_st2, tmp);
	POSTING_READ(aud_cntrl_st2);

	assert_pipe_disabled(dev_priv, pipe);

	/* Set ELD valid state */
	tmp = I915_READ(aud_cntrl_st2);
	DRM_DEBUG_DRIVER("HDMI audio: pin eld vld status=0x%08x\n", tmp);
	tmp |= (AUDIO_ELD_VALID_A << (pipe * 4));
	I915_WRITE(aud_cntrl_st2, tmp);
	tmp = I915_READ(aud_cntrl_st2);
	DRM_DEBUG_DRIVER("HDMI audio: eld vld status=0x%08x\n", tmp);

	/* Enable HDMI mode */
	tmp = I915_READ(aud_config);
	DRM_DEBUG_DRIVER("HDMI audio: audio conf: 0x%08x\n", tmp);
	/* clear N_programing_enable and N_value_index */
	tmp &= ~(AUD_CONFIG_N_VALUE_INDEX | AUD_CONFIG_N_PROG_ENABLE);
	I915_WRITE(aud_config, tmp);

	DRM_DEBUG_DRIVER("ELD on pipe %c\n", pipe_name(pipe));

	eldv = AUDIO_ELD_VALID_A << (pipe * 4);

	if (intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_DISPLAYPORT)) {
		DRM_DEBUG_DRIVER("ELD: DisplayPort detected\n");
		eld[5] |= (1 << 2);	/* Conn_Type, 0x1 = DisplayPort */
		I915_WRITE(aud_config, AUD_CONFIG_N_VALUE_INDEX); /* 0x1 = DP */
	} else {
		I915_WRITE(aud_config, audio_config_hdmi_pixel_clock(mode));
	}

	if (intel_eld_uptodate(connector,
			       aud_cntrl_st2, eldv,
			       aud_cntl_st, IBX_ELD_ADDRESS,
			       hdmiw_hdmiedid))
		return;

	tmp = I915_READ(aud_cntrl_st2);
	tmp &= ~eldv;
	I915_WRITE(aud_cntrl_st2, tmp);

	if (!eld[0])
		return;

	tmp = I915_READ(aud_cntl_st);
	tmp &= ~IBX_ELD_ADDRESS;
	I915_WRITE(aud_cntl_st, tmp);
	port = (tmp >> 29) & DIP_PORT_SEL_MASK;		/* DIP_Port_Select, 0x1 = PortB */
	DRM_DEBUG_DRIVER("port num:%d\n", port);

	len = min_t(int, eld[2], 21);	/* 84 bytes of hw ELD buffer */
	DRM_DEBUG_DRIVER("ELD size %d\n", len);
	for (i = 0; i < len; i++)
		I915_WRITE(hdmiw_hdmiedid, *((uint32_t *)eld + i));

	tmp = I915_READ(aud_cntrl_st2);
	tmp |= eldv;
	I915_WRITE(aud_cntrl_st2, tmp);
}

static void ironlake_write_eld(struct drm_connector *connector,
			       struct intel_encoder *encoder,
			       struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	uint8_t *eld = connector->eld;
	uint32_t eldv;
	uint32_t tmp;
	int len, i;
	int hdmiw_hdmiedid;
	int aud_config;
	int aud_cntl_st;
	int aud_cntrl_st2;
	enum pipe pipe = intel_crtc->pipe;
	enum port port;

	if (HAS_PCH_IBX(connector->dev)) {
		hdmiw_hdmiedid = IBX_HDMIW_HDMIEDID(pipe);
		aud_config = IBX_AUD_CFG(pipe);
		aud_cntl_st = IBX_AUD_CNTL_ST(pipe);
		aud_cntrl_st2 = IBX_AUD_CNTL_ST2;
	} else if (IS_VALLEYVIEW(connector->dev)) {
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

	DRM_DEBUG_DRIVER("ELD on pipe %c\n", pipe_name(pipe));

	if (IS_VALLEYVIEW(connector->dev))  {
		struct intel_digital_port *intel_dig_port;

		intel_dig_port = enc_to_dig_port(&encoder->base);
		port = intel_dig_port->port;
	} else {
		tmp = I915_READ(aud_cntl_st);
		port = (tmp >> 29) & DIP_PORT_SEL_MASK;
		/* DIP_Port_Select, 0x1 = PortB */
	}

	if (!port) {
		DRM_DEBUG_DRIVER("Audio directed to unknown port\n");
		/* operate blindly on all ports */
		eldv = IBX_ELD_VALIDB;
		eldv |= IBX_ELD_VALIDB << 4;
		eldv |= IBX_ELD_VALIDB << 8;
	} else {
		DRM_DEBUG_DRIVER("ELD on port %c\n", port_name(port));
		eldv = IBX_ELD_VALIDB << ((port - 1) * 4);
	}

	if (intel_pipe_has_type(intel_crtc, INTEL_OUTPUT_DISPLAYPORT)) {
		DRM_DEBUG_DRIVER("ELD: DisplayPort detected\n");
		eld[5] |= (1 << 2);	/* Conn_Type, 0x1 = DisplayPort */
		I915_WRITE(aud_config, AUD_CONFIG_N_VALUE_INDEX); /* 0x1 = DP */
	} else {
		I915_WRITE(aud_config, audio_config_hdmi_pixel_clock(mode));
	}

	if (intel_eld_uptodate(connector,
			       aud_cntrl_st2, eldv,
			       aud_cntl_st, IBX_ELD_ADDRESS,
			       hdmiw_hdmiedid))
		return;

	tmp = I915_READ(aud_cntrl_st2);
	tmp &= ~eldv;
	I915_WRITE(aud_cntrl_st2, tmp);

	if (!eld[0])
		return;

	tmp = I915_READ(aud_cntl_st);
	tmp &= ~IBX_ELD_ADDRESS;
	I915_WRITE(aud_cntl_st, tmp);

	len = min_t(int, eld[2], 21);	/* 84 bytes of hw ELD buffer */
	DRM_DEBUG_DRIVER("ELD size %d\n", len);
	for (i = 0; i < len; i++)
		I915_WRITE(hdmiw_hdmiedid, *((uint32_t *)eld + i));

	tmp = I915_READ(aud_cntrl_st2);
	tmp |= eldv;
	I915_WRITE(aud_cntrl_st2, tmp);
}

void intel_write_eld(struct intel_encoder *intel_encoder)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct intel_crtc *crtc = to_intel_crtc(encoder->crtc);
	struct drm_display_mode *mode = &crtc->config.adjusted_mode;
	struct drm_connector *connector;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	connector = drm_select_eld(encoder, mode);
	if (!connector)
		return;

	DRM_DEBUG_DRIVER("ELD on [CONNECTOR:%d:%s], [ENCODER:%d:%s]\n",
			 connector->base.id,
			 connector->name,
			 connector->encoder->base.id,
			 connector->encoder->name);

	connector->eld[6] = drm_av_sync_delay(connector, mode) / 2;

	if (dev_priv->display.write_eld)
		dev_priv->display.write_eld(connector, intel_encoder, mode);
}

/**
 * intel_init_audio - Set up chip specific audio functions
 * @dev: drm device
 */
void intel_init_audio(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (IS_G4X(dev))
		dev_priv->display.write_eld = g4x_write_eld;
	else if (IS_VALLEYVIEW(dev))
		dev_priv->display.write_eld = ironlake_write_eld;
	else if (IS_HASWELL(dev) || INTEL_INFO(dev)->gen >= 8)
		dev_priv->display.write_eld = haswell_write_eld;
	else if (HAS_PCH_SPLIT(dev))
		dev_priv->display.write_eld = ironlake_write_eld;
}

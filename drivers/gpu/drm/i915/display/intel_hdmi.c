/*
 * Copyright 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2009 Intel Corporation
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
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Jesse Barnes <jesse.barnes@intel.com>
 */

#include <linux/delay.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>

#include <drm/display/drm_hdcp_helper.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_scdc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/intel_lpe_audio.h>

#include "g4x_hdmi.h"
#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_atomic.h"
#include "intel_audio.h"
#include "intel_connector.h"
#include "intel_cx0_phy.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_gmbus.h"
#include "intel_hdcp.h"
#include "intel_hdcp_regs.h"
#include "intel_hdmi.h"
#include "intel_lspcon.h"
#include "intel_panel.h"
#include "intel_snps_phy.h"

inline struct drm_i915_private *intel_hdmi_to_i915(struct intel_hdmi *intel_hdmi)
{
	return to_i915(hdmi_to_dig_port(intel_hdmi)->base.base.dev);
}

static void
assert_hdmi_port_disabled(struct intel_hdmi *intel_hdmi)
{
	struct drm_i915_private *dev_priv = intel_hdmi_to_i915(intel_hdmi);
	u32 enabled_bits;

	enabled_bits = HAS_DDI(dev_priv) ? DDI_BUF_CTL_ENABLE : SDVO_ENABLE;

	drm_WARN(&dev_priv->drm,
		 intel_de_read(dev_priv, intel_hdmi->hdmi_reg) & enabled_bits,
		 "HDMI port enabled, expecting disabled\n");
}

static void
assert_hdmi_transcoder_func_disabled(struct drm_i915_private *dev_priv,
				     enum transcoder cpu_transcoder)
{
	drm_WARN(&dev_priv->drm,
		 intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder)) &
		 TRANS_DDI_FUNC_ENABLE,
		 "HDMI transcoder function enabled, expecting disabled\n");
}

static u32 g4x_infoframe_index(unsigned int type)
{
	switch (type) {
	case HDMI_PACKET_TYPE_GAMUT_METADATA:
		return VIDEO_DIP_SELECT_GAMUT;
	case HDMI_INFOFRAME_TYPE_AVI:
		return VIDEO_DIP_SELECT_AVI;
	case HDMI_INFOFRAME_TYPE_SPD:
		return VIDEO_DIP_SELECT_SPD;
	case HDMI_INFOFRAME_TYPE_VENDOR:
		return VIDEO_DIP_SELECT_VENDOR;
	default:
		MISSING_CASE(type);
		return 0;
	}
}

static u32 g4x_infoframe_enable(unsigned int type)
{
	switch (type) {
	case HDMI_PACKET_TYPE_GENERAL_CONTROL:
		return VIDEO_DIP_ENABLE_GCP;
	case HDMI_PACKET_TYPE_GAMUT_METADATA:
		return VIDEO_DIP_ENABLE_GAMUT;
	case DP_SDP_VSC:
		return 0;
	case HDMI_INFOFRAME_TYPE_AVI:
		return VIDEO_DIP_ENABLE_AVI;
	case HDMI_INFOFRAME_TYPE_SPD:
		return VIDEO_DIP_ENABLE_SPD;
	case HDMI_INFOFRAME_TYPE_VENDOR:
		return VIDEO_DIP_ENABLE_VENDOR;
	case HDMI_INFOFRAME_TYPE_DRM:
		return 0;
	default:
		MISSING_CASE(type);
		return 0;
	}
}

static u32 hsw_infoframe_enable(unsigned int type)
{
	switch (type) {
	case HDMI_PACKET_TYPE_GENERAL_CONTROL:
		return VIDEO_DIP_ENABLE_GCP_HSW;
	case HDMI_PACKET_TYPE_GAMUT_METADATA:
		return VIDEO_DIP_ENABLE_GMP_HSW;
	case DP_SDP_VSC:
		return VIDEO_DIP_ENABLE_VSC_HSW;
	case DP_SDP_PPS:
		return VDIP_ENABLE_PPS;
	case HDMI_INFOFRAME_TYPE_AVI:
		return VIDEO_DIP_ENABLE_AVI_HSW;
	case HDMI_INFOFRAME_TYPE_SPD:
		return VIDEO_DIP_ENABLE_SPD_HSW;
	case HDMI_INFOFRAME_TYPE_VENDOR:
		return VIDEO_DIP_ENABLE_VS_HSW;
	case HDMI_INFOFRAME_TYPE_DRM:
		return VIDEO_DIP_ENABLE_DRM_GLK;
	default:
		MISSING_CASE(type);
		return 0;
	}
}

static i915_reg_t
hsw_dip_data_reg(struct drm_i915_private *dev_priv,
		 enum transcoder cpu_transcoder,
		 unsigned int type,
		 int i)
{
	switch (type) {
	case HDMI_PACKET_TYPE_GAMUT_METADATA:
		return HSW_TVIDEO_DIP_GMP_DATA(cpu_transcoder, i);
	case DP_SDP_VSC:
		return HSW_TVIDEO_DIP_VSC_DATA(cpu_transcoder, i);
	case DP_SDP_PPS:
		return ICL_VIDEO_DIP_PPS_DATA(cpu_transcoder, i);
	case HDMI_INFOFRAME_TYPE_AVI:
		return HSW_TVIDEO_DIP_AVI_DATA(cpu_transcoder, i);
	case HDMI_INFOFRAME_TYPE_SPD:
		return HSW_TVIDEO_DIP_SPD_DATA(cpu_transcoder, i);
	case HDMI_INFOFRAME_TYPE_VENDOR:
		return HSW_TVIDEO_DIP_VS_DATA(cpu_transcoder, i);
	case HDMI_INFOFRAME_TYPE_DRM:
		return GLK_TVIDEO_DIP_DRM_DATA(cpu_transcoder, i);
	default:
		MISSING_CASE(type);
		return INVALID_MMIO_REG;
	}
}

static int hsw_dip_data_size(struct drm_i915_private *dev_priv,
			     unsigned int type)
{
	switch (type) {
	case DP_SDP_VSC:
		return VIDEO_DIP_VSC_DATA_SIZE;
	case DP_SDP_PPS:
		return VIDEO_DIP_PPS_DATA_SIZE;
	case HDMI_PACKET_TYPE_GAMUT_METADATA:
		if (DISPLAY_VER(dev_priv) >= 11)
			return VIDEO_DIP_GMP_DATA_SIZE;
		else
			return VIDEO_DIP_DATA_SIZE;
	default:
		return VIDEO_DIP_DATA_SIZE;
	}
}

static void g4x_write_infoframe(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state,
				unsigned int type,
				const void *frame, ssize_t len)
{
	const u32 *data = frame;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 val = intel_de_read(dev_priv, VIDEO_DIP_CTL);
	int i;

	drm_WARN(&dev_priv->drm, !(val & VIDEO_DIP_ENABLE),
		 "Writing DIP with CTL reg disabled\n");

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	val &= ~g4x_infoframe_enable(type);

	intel_de_write(dev_priv, VIDEO_DIP_CTL, val);

	for (i = 0; i < len; i += 4) {
		intel_de_write(dev_priv, VIDEO_DIP_DATA, *data);
		data++;
	}
	/* Write every possible data byte to force correct ECC calculation. */
	for (; i < VIDEO_DIP_DATA_SIZE; i += 4)
		intel_de_write(dev_priv, VIDEO_DIP_DATA, 0);

	val |= g4x_infoframe_enable(type);
	val &= ~VIDEO_DIP_FREQ_MASK;
	val |= VIDEO_DIP_FREQ_VSYNC;

	intel_de_write(dev_priv, VIDEO_DIP_CTL, val);
	intel_de_posting_read(dev_priv, VIDEO_DIP_CTL);
}

static void g4x_read_infoframe(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type,
			       void *frame, ssize_t len)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 *data = frame;
	int i;

	intel_de_rmw(dev_priv, VIDEO_DIP_CTL,
		     VIDEO_DIP_SELECT_MASK | 0xf, g4x_infoframe_index(type));

	for (i = 0; i < len; i += 4)
		*data++ = intel_de_read(dev_priv, VIDEO_DIP_DATA);
}

static u32 g4x_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 val = intel_de_read(dev_priv, VIDEO_DIP_CTL);

	if ((val & VIDEO_DIP_ENABLE) == 0)
		return 0;

	if ((val & VIDEO_DIP_PORT_MASK) != VIDEO_DIP_PORT(encoder->port))
		return 0;

	return val & (VIDEO_DIP_ENABLE_AVI |
		      VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_SPD);
}

static void ibx_write_infoframe(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state,
				unsigned int type,
				const void *frame, ssize_t len)
{
	const u32 *data = frame;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	i915_reg_t reg = TVIDEO_DIP_CTL(crtc->pipe);
	u32 val = intel_de_read(dev_priv, reg);
	int i;

	drm_WARN(&dev_priv->drm, !(val & VIDEO_DIP_ENABLE),
		 "Writing DIP with CTL reg disabled\n");

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	val &= ~g4x_infoframe_enable(type);

	intel_de_write(dev_priv, reg, val);

	for (i = 0; i < len; i += 4) {
		intel_de_write(dev_priv, TVIDEO_DIP_DATA(crtc->pipe),
			       *data);
		data++;
	}
	/* Write every possible data byte to force correct ECC calculation. */
	for (; i < VIDEO_DIP_DATA_SIZE; i += 4)
		intel_de_write(dev_priv, TVIDEO_DIP_DATA(crtc->pipe), 0);

	val |= g4x_infoframe_enable(type);
	val &= ~VIDEO_DIP_FREQ_MASK;
	val |= VIDEO_DIP_FREQ_VSYNC;

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);
}

static void ibx_read_infoframe(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type,
			       void *frame, ssize_t len)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 *data = frame;
	int i;

	intel_de_rmw(dev_priv, TVIDEO_DIP_CTL(crtc->pipe),
		     VIDEO_DIP_SELECT_MASK | 0xf, g4x_infoframe_index(type));

	for (i = 0; i < len; i += 4)
		*data++ = intel_de_read(dev_priv, TVIDEO_DIP_DATA(crtc->pipe));
}

static u32 ibx_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum pipe pipe = to_intel_crtc(pipe_config->uapi.crtc)->pipe;
	i915_reg_t reg = TVIDEO_DIP_CTL(pipe);
	u32 val = intel_de_read(dev_priv, reg);

	if ((val & VIDEO_DIP_ENABLE) == 0)
		return 0;

	if ((val & VIDEO_DIP_PORT_MASK) != VIDEO_DIP_PORT(encoder->port))
		return 0;

	return val & (VIDEO_DIP_ENABLE_AVI |
		      VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
		      VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);
}

static void cpt_write_infoframe(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state,
				unsigned int type,
				const void *frame, ssize_t len)
{
	const u32 *data = frame;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	i915_reg_t reg = TVIDEO_DIP_CTL(crtc->pipe);
	u32 val = intel_de_read(dev_priv, reg);
	int i;

	drm_WARN(&dev_priv->drm, !(val & VIDEO_DIP_ENABLE),
		 "Writing DIP with CTL reg disabled\n");

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	/* The DIP control register spec says that we need to update the AVI
	 * infoframe without clearing its enable bit */
	if (type != HDMI_INFOFRAME_TYPE_AVI)
		val &= ~g4x_infoframe_enable(type);

	intel_de_write(dev_priv, reg, val);

	for (i = 0; i < len; i += 4) {
		intel_de_write(dev_priv, TVIDEO_DIP_DATA(crtc->pipe),
			       *data);
		data++;
	}
	/* Write every possible data byte to force correct ECC calculation. */
	for (; i < VIDEO_DIP_DATA_SIZE; i += 4)
		intel_de_write(dev_priv, TVIDEO_DIP_DATA(crtc->pipe), 0);

	val |= g4x_infoframe_enable(type);
	val &= ~VIDEO_DIP_FREQ_MASK;
	val |= VIDEO_DIP_FREQ_VSYNC;

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);
}

static void cpt_read_infoframe(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type,
			       void *frame, ssize_t len)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 *data = frame;
	int i;

	intel_de_rmw(dev_priv, TVIDEO_DIP_CTL(crtc->pipe),
		     VIDEO_DIP_SELECT_MASK | 0xf, g4x_infoframe_index(type));

	for (i = 0; i < len; i += 4)
		*data++ = intel_de_read(dev_priv, TVIDEO_DIP_DATA(crtc->pipe));
}

static u32 cpt_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum pipe pipe = to_intel_crtc(pipe_config->uapi.crtc)->pipe;
	u32 val = intel_de_read(dev_priv, TVIDEO_DIP_CTL(pipe));

	if ((val & VIDEO_DIP_ENABLE) == 0)
		return 0;

	return val & (VIDEO_DIP_ENABLE_AVI |
		      VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
		      VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);
}

static void vlv_write_infoframe(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state,
				unsigned int type,
				const void *frame, ssize_t len)
{
	const u32 *data = frame;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	i915_reg_t reg = VLV_TVIDEO_DIP_CTL(crtc->pipe);
	u32 val = intel_de_read(dev_priv, reg);
	int i;

	drm_WARN(&dev_priv->drm, !(val & VIDEO_DIP_ENABLE),
		 "Writing DIP with CTL reg disabled\n");

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	val &= ~g4x_infoframe_enable(type);

	intel_de_write(dev_priv, reg, val);

	for (i = 0; i < len; i += 4) {
		intel_de_write(dev_priv,
			       VLV_TVIDEO_DIP_DATA(crtc->pipe), *data);
		data++;
	}
	/* Write every possible data byte to force correct ECC calculation. */
	for (; i < VIDEO_DIP_DATA_SIZE; i += 4)
		intel_de_write(dev_priv,
			       VLV_TVIDEO_DIP_DATA(crtc->pipe), 0);

	val |= g4x_infoframe_enable(type);
	val &= ~VIDEO_DIP_FREQ_MASK;
	val |= VIDEO_DIP_FREQ_VSYNC;

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);
}

static void vlv_read_infoframe(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type,
			       void *frame, ssize_t len)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	u32 *data = frame;
	int i;

	intel_de_rmw(dev_priv, VLV_TVIDEO_DIP_CTL(crtc->pipe),
		     VIDEO_DIP_SELECT_MASK | 0xf, g4x_infoframe_index(type));

	for (i = 0; i < len; i += 4)
		*data++ = intel_de_read(dev_priv,
				        VLV_TVIDEO_DIP_DATA(crtc->pipe));
}

static u32 vlv_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum pipe pipe = to_intel_crtc(pipe_config->uapi.crtc)->pipe;
	u32 val = intel_de_read(dev_priv, VLV_TVIDEO_DIP_CTL(pipe));

	if ((val & VIDEO_DIP_ENABLE) == 0)
		return 0;

	if ((val & VIDEO_DIP_PORT_MASK) != VIDEO_DIP_PORT(encoder->port))
		return 0;

	return val & (VIDEO_DIP_ENABLE_AVI |
		      VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
		      VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);
}

void hsw_write_infoframe(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state,
			 unsigned int type,
			 const void *frame, ssize_t len)
{
	const u32 *data = frame;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	i915_reg_t ctl_reg = HSW_TVIDEO_DIP_CTL(cpu_transcoder);
	int data_size;
	int i;
	u32 val = intel_de_read(dev_priv, ctl_reg);

	data_size = hsw_dip_data_size(dev_priv, type);

	drm_WARN_ON(&dev_priv->drm, len > data_size);

	val &= ~hsw_infoframe_enable(type);
	intel_de_write(dev_priv, ctl_reg, val);

	for (i = 0; i < len; i += 4) {
		intel_de_write(dev_priv,
			       hsw_dip_data_reg(dev_priv, cpu_transcoder, type, i >> 2),
			       *data);
		data++;
	}
	/* Write every possible data byte to force correct ECC calculation. */
	for (; i < data_size; i += 4)
		intel_de_write(dev_priv,
			       hsw_dip_data_reg(dev_priv, cpu_transcoder, type, i >> 2),
			       0);

	/* Wa_14013475917 */
	if (IS_DISPLAY_VER(dev_priv, 13, 14) && crtc_state->has_psr && type == DP_SDP_VSC)
		return;

	val |= hsw_infoframe_enable(type);
	intel_de_write(dev_priv, ctl_reg, val);
	intel_de_posting_read(dev_priv, ctl_reg);
}

void hsw_read_infoframe(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			unsigned int type, void *frame, ssize_t len)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 *data = frame;
	int i;

	for (i = 0; i < len; i += 4)
		*data++ = intel_de_read(dev_priv,
				        hsw_dip_data_reg(dev_priv, cpu_transcoder, type, i >> 2));
}

static u32 hsw_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 val = intel_de_read(dev_priv,
				HSW_TVIDEO_DIP_CTL(pipe_config->cpu_transcoder));
	u32 mask;

	mask = (VIDEO_DIP_ENABLE_VSC_HSW | VIDEO_DIP_ENABLE_AVI_HSW |
		VIDEO_DIP_ENABLE_GCP_HSW | VIDEO_DIP_ENABLE_VS_HSW |
		VIDEO_DIP_ENABLE_GMP_HSW | VIDEO_DIP_ENABLE_SPD_HSW);

	if (DISPLAY_VER(dev_priv) >= 10)
		mask |= VIDEO_DIP_ENABLE_DRM_GLK;

	return val & mask;
}

static const u8 infoframe_type_to_idx[] = {
	HDMI_PACKET_TYPE_GENERAL_CONTROL,
	HDMI_PACKET_TYPE_GAMUT_METADATA,
	DP_SDP_VSC,
	HDMI_INFOFRAME_TYPE_AVI,
	HDMI_INFOFRAME_TYPE_SPD,
	HDMI_INFOFRAME_TYPE_VENDOR,
	HDMI_INFOFRAME_TYPE_DRM,
};

u32 intel_hdmi_infoframe_enable(unsigned int type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(infoframe_type_to_idx); i++) {
		if (infoframe_type_to_idx[i] == type)
			return BIT(i);
	}

	return 0;
}

u32 intel_hdmi_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	u32 val, ret = 0;
	int i;

	val = dig_port->infoframes_enabled(encoder, crtc_state);

	/* map from hardware bits to dip idx */
	for (i = 0; i < ARRAY_SIZE(infoframe_type_to_idx); i++) {
		unsigned int type = infoframe_type_to_idx[i];

		if (HAS_DDI(dev_priv)) {
			if (val & hsw_infoframe_enable(type))
				ret |= BIT(i);
		} else {
			if (val & g4x_infoframe_enable(type))
				ret |= BIT(i);
		}
	}

	return ret;
}

/*
 * The data we write to the DIP data buffer registers is 1 byte bigger than the
 * HDMI infoframe size because of an ECC/reserved byte at position 3 (starting
 * at 0). It's also a byte used by DisplayPort so the same DIP registers can be
 * used for both technologies.
 *
 * DW0: Reserved/ECC/DP | HB2 | HB1 | HB0
 * DW1:       DB3       | DB2 | DB1 | DB0
 * DW2:       DB7       | DB6 | DB5 | DB4
 * DW3: ...
 *
 * (HB is Header Byte, DB is Data Byte)
 *
 * The hdmi pack() functions don't know about that hardware specific hole so we
 * trick them by giving an offset into the buffer and moving back the header
 * bytes by one.
 */
static void intel_write_infoframe(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  enum hdmi_infoframe_type type,
				  const union hdmi_infoframe *frame)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	u8 buffer[VIDEO_DIP_DATA_SIZE];
	ssize_t len;

	if ((crtc_state->infoframes.enable &
	     intel_hdmi_infoframe_enable(type)) == 0)
		return;

	if (drm_WARN_ON(encoder->base.dev, frame->any.type != type))
		return;

	/* see comment above for the reason for this offset */
	len = hdmi_infoframe_pack_only(frame, buffer + 1, sizeof(buffer) - 1);
	if (drm_WARN_ON(encoder->base.dev, len < 0))
		return;

	/* Insert the 'hole' (see big comment above) at position 3 */
	memmove(&buffer[0], &buffer[1], 3);
	buffer[3] = 0;
	len++;

	dig_port->write_infoframe(encoder, crtc_state, type, buffer, len);
}

void intel_read_infoframe(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state,
			  enum hdmi_infoframe_type type,
			  union hdmi_infoframe *frame)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	u8 buffer[VIDEO_DIP_DATA_SIZE];
	int ret;

	if ((crtc_state->infoframes.enable &
	     intel_hdmi_infoframe_enable(type)) == 0)
		return;

	dig_port->read_infoframe(encoder, crtc_state,
				       type, buffer, sizeof(buffer));

	/* Fill the 'hole' (see big comment above) at position 3 */
	memmove(&buffer[1], &buffer[0], 3);

	/* see comment above for the reason for this offset */
	ret = hdmi_infoframe_unpack(frame, buffer + 1, sizeof(buffer) - 1);
	if (ret) {
		drm_dbg_kms(encoder->base.dev,
			    "Failed to unpack infoframe type 0x%02x\n", type);
		return;
	}

	if (frame->any.type != type)
		drm_dbg_kms(encoder->base.dev,
			    "Found the wrong infoframe type 0x%x (expected 0x%02x)\n",
			    frame->any.type, type);
}

static bool
intel_hdmi_compute_avi_infoframe(struct intel_encoder *encoder,
				 struct intel_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct hdmi_avi_infoframe *frame = &crtc_state->infoframes.avi.avi;
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	struct drm_connector *connector = conn_state->connector;
	int ret;

	if (!crtc_state->has_infoframe)
		return true;

	crtc_state->infoframes.enable |=
		intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_AVI);

	ret = drm_hdmi_avi_infoframe_from_display_mode(frame, connector,
						       adjusted_mode);
	if (ret)
		return false;

	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		frame->colorspace = HDMI_COLORSPACE_YUV420;
	else if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444)
		frame->colorspace = HDMI_COLORSPACE_YUV444;
	else
		frame->colorspace = HDMI_COLORSPACE_RGB;

	drm_hdmi_avi_infoframe_colorimetry(frame, conn_state);

	/* nonsense combination */
	drm_WARN_ON(encoder->base.dev, crtc_state->limited_color_range &&
		    crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB);

	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_RGB) {
		drm_hdmi_avi_infoframe_quant_range(frame, connector,
						   adjusted_mode,
						   crtc_state->limited_color_range ?
						   HDMI_QUANTIZATION_RANGE_LIMITED :
						   HDMI_QUANTIZATION_RANGE_FULL);
	} else {
		frame->quantization_range = HDMI_QUANTIZATION_RANGE_DEFAULT;
		frame->ycc_quantization_range = HDMI_YCC_QUANTIZATION_RANGE_LIMITED;
	}

	drm_hdmi_avi_infoframe_content_type(frame, conn_state);

	/* TODO: handle pixel repetition for YCBCR420 outputs */

	ret = hdmi_avi_infoframe_check(frame);
	if (drm_WARN_ON(encoder->base.dev, ret))
		return false;

	return true;
}

static bool
intel_hdmi_compute_spd_infoframe(struct intel_encoder *encoder,
				 struct intel_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct hdmi_spd_infoframe *frame = &crtc_state->infoframes.spd.spd;
	int ret;

	if (!crtc_state->has_infoframe)
		return true;

	crtc_state->infoframes.enable |=
		intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_SPD);

	if (IS_DGFX(i915))
		ret = hdmi_spd_infoframe_init(frame, "Intel", "Discrete gfx");
	else
		ret = hdmi_spd_infoframe_init(frame, "Intel", "Integrated gfx");

	if (drm_WARN_ON(encoder->base.dev, ret))
		return false;

	frame->sdi = HDMI_SPD_SDI_PC;

	ret = hdmi_spd_infoframe_check(frame);
	if (drm_WARN_ON(encoder->base.dev, ret))
		return false;

	return true;
}

static bool
intel_hdmi_compute_hdmi_infoframe(struct intel_encoder *encoder,
				  struct intel_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state)
{
	struct hdmi_vendor_infoframe *frame =
		&crtc_state->infoframes.hdmi.vendor.hdmi;
	const struct drm_display_info *info =
		&conn_state->connector->display_info;
	int ret;

	if (!crtc_state->has_infoframe || !info->has_hdmi_infoframe)
		return true;

	crtc_state->infoframes.enable |=
		intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_VENDOR);

	ret = drm_hdmi_vendor_infoframe_from_display_mode(frame,
							  conn_state->connector,
							  &crtc_state->hw.adjusted_mode);
	if (drm_WARN_ON(encoder->base.dev, ret))
		return false;

	ret = hdmi_vendor_infoframe_check(frame);
	if (drm_WARN_ON(encoder->base.dev, ret))
		return false;

	return true;
}

static bool
intel_hdmi_compute_drm_infoframe(struct intel_encoder *encoder,
				 struct intel_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct hdmi_drm_infoframe *frame = &crtc_state->infoframes.drm.drm;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int ret;

	if (DISPLAY_VER(dev_priv) < 10)
		return true;

	if (!crtc_state->has_infoframe)
		return true;

	if (!conn_state->hdr_output_metadata)
		return true;

	crtc_state->infoframes.enable |=
		intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_DRM);

	ret = drm_hdmi_infoframe_set_hdr_metadata(frame, conn_state);
	if (ret < 0) {
		drm_dbg_kms(&dev_priv->drm,
			    "couldn't set HDR metadata in infoframe\n");
		return false;
	}

	ret = hdmi_drm_infoframe_check(frame);
	if (drm_WARN_ON(&dev_priv->drm, ret))
		return false;

	return true;
}

static void g4x_set_infoframes(struct intel_encoder *encoder,
			       bool enable,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_hdmi *intel_hdmi = &dig_port->hdmi;
	i915_reg_t reg = VIDEO_DIP_CTL;
	u32 val = intel_de_read(dev_priv, reg);
	u32 port = VIDEO_DIP_PORT(encoder->port);

	assert_hdmi_port_disabled(intel_hdmi);

	/* If the registers were not initialized yet, they might be zeroes,
	 * which means we're selecting the AVI DIP and we're setting its
	 * frequency to once. This seems to really confuse the HW and make
	 * things stop working (the register spec says the AVI always needs to
	 * be sent every VSync). So here we avoid writing to the register more
	 * than we need and also explicitly select the AVI DIP and explicitly
	 * set its frequency to every VSync. Avoiding to write it twice seems to
	 * be enough to solve the problem, but being defensive shouldn't hurt us
	 * either. */
	val |= VIDEO_DIP_SELECT_AVI | VIDEO_DIP_FREQ_VSYNC;

	if (!enable) {
		if (!(val & VIDEO_DIP_ENABLE))
			return;
		if (port != (val & VIDEO_DIP_PORT_MASK)) {
			drm_dbg_kms(&dev_priv->drm,
				    "video DIP still enabled on port %c\n",
				    (val & VIDEO_DIP_PORT_MASK) >> 29);
			return;
		}
		val &= ~(VIDEO_DIP_ENABLE | VIDEO_DIP_ENABLE_AVI |
			 VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_SPD);
		intel_de_write(dev_priv, reg, val);
		intel_de_posting_read(dev_priv, reg);
		return;
	}

	if (port != (val & VIDEO_DIP_PORT_MASK)) {
		if (val & VIDEO_DIP_ENABLE) {
			drm_dbg_kms(&dev_priv->drm,
				    "video DIP already enabled on port %c\n",
				    (val & VIDEO_DIP_PORT_MASK) >> 29);
			return;
		}
		val &= ~VIDEO_DIP_PORT_MASK;
		val |= port;
	}

	val |= VIDEO_DIP_ENABLE;
	val &= ~(VIDEO_DIP_ENABLE_AVI |
		 VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_SPD);

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);

	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_AVI,
			      &crtc_state->infoframes.avi);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_SPD,
			      &crtc_state->infoframes.spd);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_VENDOR,
			      &crtc_state->infoframes.hdmi);
}

/*
 * Determine if default_phase=1 can be indicated in the GCP infoframe.
 *
 * From HDMI specification 1.4a:
 * - The first pixel of each Video Data Period shall always have a pixel packing phase of 0
 * - The first pixel following each Video Data Period shall have a pixel packing phase of 0
 * - The PP bits shall be constant for all GCPs and will be equal to the last packing phase
 * - The first pixel following every transition of HSYNC or VSYNC shall have a pixel packing
 *   phase of 0
 */
static bool gcp_default_phase_possible(int pipe_bpp,
				       const struct drm_display_mode *mode)
{
	unsigned int pixels_per_group;

	switch (pipe_bpp) {
	case 30:
		/* 4 pixels in 5 clocks */
		pixels_per_group = 4;
		break;
	case 36:
		/* 2 pixels in 3 clocks */
		pixels_per_group = 2;
		break;
	case 48:
		/* 1 pixel in 2 clocks */
		pixels_per_group = 1;
		break;
	default:
		/* phase information not relevant for 8bpc */
		return false;
	}

	return mode->crtc_hdisplay % pixels_per_group == 0 &&
		mode->crtc_htotal % pixels_per_group == 0 &&
		mode->crtc_hblank_start % pixels_per_group == 0 &&
		mode->crtc_hblank_end % pixels_per_group == 0 &&
		mode->crtc_hsync_start % pixels_per_group == 0 &&
		mode->crtc_hsync_end % pixels_per_group == 0 &&
		((mode->flags & DRM_MODE_FLAG_INTERLACE) == 0 ||
		 mode->crtc_htotal/2 % pixels_per_group == 0);
}

static bool intel_hdmi_set_gcp_infoframe(struct intel_encoder *encoder,
					 const struct intel_crtc_state *crtc_state,
					 const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	i915_reg_t reg;

	if ((crtc_state->infoframes.enable &
	     intel_hdmi_infoframe_enable(HDMI_PACKET_TYPE_GENERAL_CONTROL)) == 0)
		return false;

	if (HAS_DDI(dev_priv))
		reg = HSW_TVIDEO_DIP_GCP(crtc_state->cpu_transcoder);
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		reg = VLV_TVIDEO_DIP_GCP(crtc->pipe);
	else if (HAS_PCH_SPLIT(dev_priv))
		reg = TVIDEO_DIP_GCP(crtc->pipe);
	else
		return false;

	intel_de_write(dev_priv, reg, crtc_state->infoframes.gcp);

	return true;
}

void intel_hdmi_read_gcp_infoframe(struct intel_encoder *encoder,
				   struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	i915_reg_t reg;

	if ((crtc_state->infoframes.enable &
	     intel_hdmi_infoframe_enable(HDMI_PACKET_TYPE_GENERAL_CONTROL)) == 0)
		return;

	if (HAS_DDI(dev_priv))
		reg = HSW_TVIDEO_DIP_GCP(crtc_state->cpu_transcoder);
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		reg = VLV_TVIDEO_DIP_GCP(crtc->pipe);
	else if (HAS_PCH_SPLIT(dev_priv))
		reg = TVIDEO_DIP_GCP(crtc->pipe);
	else
		return;

	crtc_state->infoframes.gcp = intel_de_read(dev_priv, reg);
}

static void intel_hdmi_compute_gcp_infoframe(struct intel_encoder *encoder,
					     struct intel_crtc_state *crtc_state,
					     struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (IS_G4X(dev_priv) || !crtc_state->has_infoframe)
		return;

	crtc_state->infoframes.enable |=
		intel_hdmi_infoframe_enable(HDMI_PACKET_TYPE_GENERAL_CONTROL);

	/* Indicate color indication for deep color mode */
	if (crtc_state->pipe_bpp > 24)
		crtc_state->infoframes.gcp |= GCP_COLOR_INDICATION;

	/* Enable default_phase whenever the display mode is suitably aligned */
	if (gcp_default_phase_possible(crtc_state->pipe_bpp,
				       &crtc_state->hw.adjusted_mode))
		crtc_state->infoframes.gcp |= GCP_DEFAULT_PHASE_ENABLE;
}

static void ibx_set_infoframes(struct intel_encoder *encoder,
			       bool enable,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_hdmi *intel_hdmi = &dig_port->hdmi;
	i915_reg_t reg = TVIDEO_DIP_CTL(crtc->pipe);
	u32 val = intel_de_read(dev_priv, reg);
	u32 port = VIDEO_DIP_PORT(encoder->port);

	assert_hdmi_port_disabled(intel_hdmi);

	/* See the big comment in g4x_set_infoframes() */
	val |= VIDEO_DIP_SELECT_AVI | VIDEO_DIP_FREQ_VSYNC;

	if (!enable) {
		if (!(val & VIDEO_DIP_ENABLE))
			return;
		val &= ~(VIDEO_DIP_ENABLE | VIDEO_DIP_ENABLE_AVI |
			 VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
			 VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);
		intel_de_write(dev_priv, reg, val);
		intel_de_posting_read(dev_priv, reg);
		return;
	}

	if (port != (val & VIDEO_DIP_PORT_MASK)) {
		drm_WARN(&dev_priv->drm, val & VIDEO_DIP_ENABLE,
			 "DIP already enabled on port %c\n",
			 (val & VIDEO_DIP_PORT_MASK) >> 29);
		val &= ~VIDEO_DIP_PORT_MASK;
		val |= port;
	}

	val |= VIDEO_DIP_ENABLE;
	val &= ~(VIDEO_DIP_ENABLE_AVI |
		 VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
		 VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);

	if (intel_hdmi_set_gcp_infoframe(encoder, crtc_state, conn_state))
		val |= VIDEO_DIP_ENABLE_GCP;

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);

	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_AVI,
			      &crtc_state->infoframes.avi);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_SPD,
			      &crtc_state->infoframes.spd);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_VENDOR,
			      &crtc_state->infoframes.hdmi);
}

static void cpt_set_infoframes(struct intel_encoder *encoder,
			       bool enable,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);
	i915_reg_t reg = TVIDEO_DIP_CTL(crtc->pipe);
	u32 val = intel_de_read(dev_priv, reg);

	assert_hdmi_port_disabled(intel_hdmi);

	/* See the big comment in g4x_set_infoframes() */
	val |= VIDEO_DIP_SELECT_AVI | VIDEO_DIP_FREQ_VSYNC;

	if (!enable) {
		if (!(val & VIDEO_DIP_ENABLE))
			return;
		val &= ~(VIDEO_DIP_ENABLE | VIDEO_DIP_ENABLE_AVI |
			 VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
			 VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);
		intel_de_write(dev_priv, reg, val);
		intel_de_posting_read(dev_priv, reg);
		return;
	}

	/* Set both together, unset both together: see the spec. */
	val |= VIDEO_DIP_ENABLE | VIDEO_DIP_ENABLE_AVI;
	val &= ~(VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
		 VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);

	if (intel_hdmi_set_gcp_infoframe(encoder, crtc_state, conn_state))
		val |= VIDEO_DIP_ENABLE_GCP;

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);

	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_AVI,
			      &crtc_state->infoframes.avi);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_SPD,
			      &crtc_state->infoframes.spd);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_VENDOR,
			      &crtc_state->infoframes.hdmi);
}

static void vlv_set_infoframes(struct intel_encoder *encoder,
			       bool enable,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);
	i915_reg_t reg = VLV_TVIDEO_DIP_CTL(crtc->pipe);
	u32 val = intel_de_read(dev_priv, reg);
	u32 port = VIDEO_DIP_PORT(encoder->port);

	assert_hdmi_port_disabled(intel_hdmi);

	/* See the big comment in g4x_set_infoframes() */
	val |= VIDEO_DIP_SELECT_AVI | VIDEO_DIP_FREQ_VSYNC;

	if (!enable) {
		if (!(val & VIDEO_DIP_ENABLE))
			return;
		val &= ~(VIDEO_DIP_ENABLE | VIDEO_DIP_ENABLE_AVI |
			 VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
			 VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);
		intel_de_write(dev_priv, reg, val);
		intel_de_posting_read(dev_priv, reg);
		return;
	}

	if (port != (val & VIDEO_DIP_PORT_MASK)) {
		drm_WARN(&dev_priv->drm, val & VIDEO_DIP_ENABLE,
			 "DIP already enabled on port %c\n",
			 (val & VIDEO_DIP_PORT_MASK) >> 29);
		val &= ~VIDEO_DIP_PORT_MASK;
		val |= port;
	}

	val |= VIDEO_DIP_ENABLE;
	val &= ~(VIDEO_DIP_ENABLE_AVI |
		 VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
		 VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);

	if (intel_hdmi_set_gcp_infoframe(encoder, crtc_state, conn_state))
		val |= VIDEO_DIP_ENABLE_GCP;

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);

	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_AVI,
			      &crtc_state->infoframes.avi);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_SPD,
			      &crtc_state->infoframes.spd);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_VENDOR,
			      &crtc_state->infoframes.hdmi);
}

static void hsw_set_infoframes(struct intel_encoder *encoder,
			       bool enable,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	i915_reg_t reg = HSW_TVIDEO_DIP_CTL(crtc_state->cpu_transcoder);
	u32 val = intel_de_read(dev_priv, reg);

	assert_hdmi_transcoder_func_disabled(dev_priv,
					     crtc_state->cpu_transcoder);

	val &= ~(VIDEO_DIP_ENABLE_VSC_HSW | VIDEO_DIP_ENABLE_AVI_HSW |
		 VIDEO_DIP_ENABLE_GCP_HSW | VIDEO_DIP_ENABLE_VS_HSW |
		 VIDEO_DIP_ENABLE_GMP_HSW | VIDEO_DIP_ENABLE_SPD_HSW |
		 VIDEO_DIP_ENABLE_DRM_GLK);

	if (!enable) {
		intel_de_write(dev_priv, reg, val);
		intel_de_posting_read(dev_priv, reg);
		return;
	}

	if (intel_hdmi_set_gcp_infoframe(encoder, crtc_state, conn_state))
		val |= VIDEO_DIP_ENABLE_GCP_HSW;

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);

	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_AVI,
			      &crtc_state->infoframes.avi);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_SPD,
			      &crtc_state->infoframes.spd);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_VENDOR,
			      &crtc_state->infoframes.hdmi);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_DRM,
			      &crtc_state->infoframes.drm);
}

void intel_dp_dual_mode_set_tmds_output(struct intel_hdmi *hdmi, bool enable)
{
	struct drm_i915_private *dev_priv = intel_hdmi_to_i915(hdmi);
	struct i2c_adapter *ddc = hdmi->attached_connector->base.ddc;

	if (hdmi->dp_dual_mode.type < DRM_DP_DUAL_MODE_TYPE2_DVI)
		return;

	drm_dbg_kms(&dev_priv->drm, "%s DP dual mode adaptor TMDS output\n",
		    enable ? "Enabling" : "Disabling");

	drm_dp_dual_mode_set_tmds_output(&dev_priv->drm,
					 hdmi->dp_dual_mode.type, ddc, enable);
}

static int intel_hdmi_hdcp_read(struct intel_digital_port *dig_port,
				unsigned int offset, void *buffer, size_t size)
{
	struct intel_hdmi *hdmi = &dig_port->hdmi;
	struct i2c_adapter *ddc = hdmi->attached_connector->base.ddc;
	int ret;
	u8 start = offset & 0xff;
	struct i2c_msg msgs[] = {
		{
			.addr = DRM_HDCP_DDC_ADDR,
			.flags = 0,
			.len = 1,
			.buf = &start,
		},
		{
			.addr = DRM_HDCP_DDC_ADDR,
			.flags = I2C_M_RD,
			.len = size,
			.buf = buffer
		}
	};
	ret = i2c_transfer(ddc, msgs, ARRAY_SIZE(msgs));
	if (ret == ARRAY_SIZE(msgs))
		return 0;
	return ret >= 0 ? -EIO : ret;
}

static int intel_hdmi_hdcp_write(struct intel_digital_port *dig_port,
				 unsigned int offset, void *buffer, size_t size)
{
	struct intel_hdmi *hdmi = &dig_port->hdmi;
	struct i2c_adapter *ddc = hdmi->attached_connector->base.ddc;
	int ret;
	u8 *write_buf;
	struct i2c_msg msg;

	write_buf = kzalloc(size + 1, GFP_KERNEL);
	if (!write_buf)
		return -ENOMEM;

	write_buf[0] = offset & 0xff;
	memcpy(&write_buf[1], buffer, size);

	msg.addr = DRM_HDCP_DDC_ADDR;
	msg.flags = 0,
	msg.len = size + 1,
	msg.buf = write_buf;

	ret = i2c_transfer(ddc, &msg, 1);
	if (ret == 1)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

	kfree(write_buf);
	return ret;
}

static
int intel_hdmi_hdcp_write_an_aksv(struct intel_digital_port *dig_port,
				  u8 *an)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_hdmi *hdmi = &dig_port->hdmi;
	struct i2c_adapter *ddc = hdmi->attached_connector->base.ddc;
	int ret;

	ret = intel_hdmi_hdcp_write(dig_port, DRM_HDCP_DDC_AN, an,
				    DRM_HDCP_AN_LEN);
	if (ret) {
		drm_dbg_kms(&i915->drm, "Write An over DDC failed (%d)\n",
			    ret);
		return ret;
	}

	ret = intel_gmbus_output_aksv(ddc);
	if (ret < 0) {
		drm_dbg_kms(&i915->drm, "Failed to output aksv (%d)\n", ret);
		return ret;
	}
	return 0;
}

static int intel_hdmi_hdcp_read_bksv(struct intel_digital_port *dig_port,
				     u8 *bksv)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	int ret;
	ret = intel_hdmi_hdcp_read(dig_port, DRM_HDCP_DDC_BKSV, bksv,
				   DRM_HDCP_KSV_LEN);
	if (ret)
		drm_dbg_kms(&i915->drm, "Read Bksv over DDC failed (%d)\n",
			    ret);
	return ret;
}

static
int intel_hdmi_hdcp_read_bstatus(struct intel_digital_port *dig_port,
				 u8 *bstatus)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	int ret;
	ret = intel_hdmi_hdcp_read(dig_port, DRM_HDCP_DDC_BSTATUS,
				   bstatus, DRM_HDCP_BSTATUS_LEN);
	if (ret)
		drm_dbg_kms(&i915->drm, "Read bstatus over DDC failed (%d)\n",
			    ret);
	return ret;
}

static
int intel_hdmi_hdcp_repeater_present(struct intel_digital_port *dig_port,
				     bool *repeater_present)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	int ret;
	u8 val;

	ret = intel_hdmi_hdcp_read(dig_port, DRM_HDCP_DDC_BCAPS, &val, 1);
	if (ret) {
		drm_dbg_kms(&i915->drm, "Read bcaps over DDC failed (%d)\n",
			    ret);
		return ret;
	}
	*repeater_present = val & DRM_HDCP_DDC_BCAPS_REPEATER_PRESENT;
	return 0;
}

static
int intel_hdmi_hdcp_read_ri_prime(struct intel_digital_port *dig_port,
				  u8 *ri_prime)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	int ret;
	ret = intel_hdmi_hdcp_read(dig_port, DRM_HDCP_DDC_RI_PRIME,
				   ri_prime, DRM_HDCP_RI_LEN);
	if (ret)
		drm_dbg_kms(&i915->drm, "Read Ri' over DDC failed (%d)\n",
			    ret);
	return ret;
}

static
int intel_hdmi_hdcp_read_ksv_ready(struct intel_digital_port *dig_port,
				   bool *ksv_ready)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	int ret;
	u8 val;

	ret = intel_hdmi_hdcp_read(dig_port, DRM_HDCP_DDC_BCAPS, &val, 1);
	if (ret) {
		drm_dbg_kms(&i915->drm, "Read bcaps over DDC failed (%d)\n",
			    ret);
		return ret;
	}
	*ksv_ready = val & DRM_HDCP_DDC_BCAPS_KSV_FIFO_READY;
	return 0;
}

static
int intel_hdmi_hdcp_read_ksv_fifo(struct intel_digital_port *dig_port,
				  int num_downstream, u8 *ksv_fifo)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	int ret;
	ret = intel_hdmi_hdcp_read(dig_port, DRM_HDCP_DDC_KSV_FIFO,
				   ksv_fifo, num_downstream * DRM_HDCP_KSV_LEN);
	if (ret) {
		drm_dbg_kms(&i915->drm,
			    "Read ksv fifo over DDC failed (%d)\n", ret);
		return ret;
	}
	return 0;
}

static
int intel_hdmi_hdcp_read_v_prime_part(struct intel_digital_port *dig_port,
				      int i, u32 *part)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	int ret;

	if (i >= DRM_HDCP_V_PRIME_NUM_PARTS)
		return -EINVAL;

	ret = intel_hdmi_hdcp_read(dig_port, DRM_HDCP_DDC_V_PRIME(i),
				   part, DRM_HDCP_V_PRIME_PART_LEN);
	if (ret)
		drm_dbg_kms(&i915->drm, "Read V'[%d] over DDC failed (%d)\n",
			    i, ret);
	return ret;
}

static int kbl_repositioning_enc_en_signal(struct intel_connector *connector,
					   enum transcoder cpu_transcoder)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_crtc *crtc = to_intel_crtc(connector->base.state->crtc);
	u32 scanline;
	int ret;

	for (;;) {
		scanline = intel_de_read(dev_priv, PIPEDSL(crtc->pipe));
		if (scanline > 100 && scanline < 200)
			break;
		usleep_range(25, 50);
	}

	ret = intel_ddi_toggle_hdcp_bits(&dig_port->base, cpu_transcoder,
					 false, TRANS_DDI_HDCP_SIGNALLING);
	if (ret) {
		drm_err(&dev_priv->drm,
			"Disable HDCP signalling failed (%d)\n", ret);
		return ret;
	}

	ret = intel_ddi_toggle_hdcp_bits(&dig_port->base, cpu_transcoder,
					 true, TRANS_DDI_HDCP_SIGNALLING);
	if (ret) {
		drm_err(&dev_priv->drm,
			"Enable HDCP signalling failed (%d)\n", ret);
		return ret;
	}

	return 0;
}

static
int intel_hdmi_hdcp_toggle_signalling(struct intel_digital_port *dig_port,
				      enum transcoder cpu_transcoder,
				      bool enable)
{
	struct intel_hdmi *hdmi = &dig_port->hdmi;
	struct intel_connector *connector = hdmi->attached_connector;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	int ret;

	if (!enable)
		usleep_range(6, 60); /* Bspec says >= 6us */

	ret = intel_ddi_toggle_hdcp_bits(&dig_port->base,
					 cpu_transcoder, enable,
					 TRANS_DDI_HDCP_SIGNALLING);
	if (ret) {
		drm_err(&dev_priv->drm, "%s HDCP signalling failed (%d)\n",
			enable ? "Enable" : "Disable", ret);
		return ret;
	}

	/*
	 * WA: To fix incorrect positioning of the window of
	 * opportunity and enc_en signalling in KABYLAKE.
	 */
	if (IS_KABYLAKE(dev_priv) && enable)
		return kbl_repositioning_enc_en_signal(connector,
						       cpu_transcoder);

	return 0;
}

static
bool intel_hdmi_hdcp_check_link_once(struct intel_digital_port *dig_port,
				     struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	int ret;
	union {
		u32 reg;
		u8 shim[DRM_HDCP_RI_LEN];
	} ri;

	ret = intel_hdmi_hdcp_read_ri_prime(dig_port, ri.shim);
	if (ret)
		return false;

	intel_de_write(i915, HDCP_RPRIME(i915, cpu_transcoder, port), ri.reg);

	/* Wait for Ri prime match */
	if (wait_for((intel_de_read(i915, HDCP_STATUS(i915, cpu_transcoder, port)) &
		      (HDCP_STATUS_RI_MATCH | HDCP_STATUS_ENC)) ==
		     (HDCP_STATUS_RI_MATCH | HDCP_STATUS_ENC), 1)) {
		drm_dbg_kms(&i915->drm, "Ri' mismatch detected (%x)\n",
			intel_de_read(i915, HDCP_STATUS(i915, cpu_transcoder,
							port)));
		return false;
	}
	return true;
}

static
bool intel_hdmi_hdcp_check_link(struct intel_digital_port *dig_port,
				struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	int retry;

	for (retry = 0; retry < 3; retry++)
		if (intel_hdmi_hdcp_check_link_once(dig_port, connector))
			return true;

	drm_err(&i915->drm, "Link check failed\n");
	return false;
}

struct hdcp2_hdmi_msg_timeout {
	u8 msg_id;
	u16 timeout;
};

static const struct hdcp2_hdmi_msg_timeout hdcp2_msg_timeout[] = {
	{ HDCP_2_2_AKE_SEND_CERT, HDCP_2_2_CERT_TIMEOUT_MS, },
	{ HDCP_2_2_AKE_SEND_PAIRING_INFO, HDCP_2_2_PAIRING_TIMEOUT_MS, },
	{ HDCP_2_2_LC_SEND_LPRIME, HDCP_2_2_HDMI_LPRIME_TIMEOUT_MS, },
	{ HDCP_2_2_REP_SEND_RECVID_LIST, HDCP_2_2_RECVID_LIST_TIMEOUT_MS, },
	{ HDCP_2_2_REP_STREAM_READY, HDCP_2_2_STREAM_READY_TIMEOUT_MS, },
};

static
int intel_hdmi_hdcp2_read_rx_status(struct intel_digital_port *dig_port,
				    u8 *rx_status)
{
	return intel_hdmi_hdcp_read(dig_port,
				    HDCP_2_2_HDMI_REG_RXSTATUS_OFFSET,
				    rx_status,
				    HDCP_2_2_HDMI_RXSTATUS_LEN);
}

static int get_hdcp2_msg_timeout(u8 msg_id, bool is_paired)
{
	int i;

	if (msg_id == HDCP_2_2_AKE_SEND_HPRIME) {
		if (is_paired)
			return HDCP_2_2_HPRIME_PAIRED_TIMEOUT_MS;
		else
			return HDCP_2_2_HPRIME_NO_PAIRED_TIMEOUT_MS;
	}

	for (i = 0; i < ARRAY_SIZE(hdcp2_msg_timeout); i++) {
		if (hdcp2_msg_timeout[i].msg_id == msg_id)
			return hdcp2_msg_timeout[i].timeout;
	}

	return -EINVAL;
}

static int
hdcp2_detect_msg_availability(struct intel_digital_port *dig_port,
			      u8 msg_id, bool *msg_ready,
			      ssize_t *msg_sz)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	u8 rx_status[HDCP_2_2_HDMI_RXSTATUS_LEN];
	int ret;

	ret = intel_hdmi_hdcp2_read_rx_status(dig_port, rx_status);
	if (ret < 0) {
		drm_dbg_kms(&i915->drm, "rx_status read failed. Err %d\n",
			    ret);
		return ret;
	}

	*msg_sz = ((HDCP_2_2_HDMI_RXSTATUS_MSG_SZ_HI(rx_status[1]) << 8) |
		  rx_status[0]);

	if (msg_id == HDCP_2_2_REP_SEND_RECVID_LIST)
		*msg_ready = (HDCP_2_2_HDMI_RXSTATUS_READY(rx_status[1]) &&
			     *msg_sz);
	else
		*msg_ready = *msg_sz;

	return 0;
}

static ssize_t
intel_hdmi_hdcp2_wait_for_msg(struct intel_digital_port *dig_port,
			      u8 msg_id, bool paired)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	bool msg_ready = false;
	int timeout, ret;
	ssize_t msg_sz = 0;

	timeout = get_hdcp2_msg_timeout(msg_id, paired);
	if (timeout < 0)
		return timeout;

	ret = __wait_for(ret = hdcp2_detect_msg_availability(dig_port,
							     msg_id, &msg_ready,
							     &msg_sz),
			 !ret && msg_ready && msg_sz, timeout * 1000,
			 1000, 5 * 1000);
	if (ret)
		drm_dbg_kms(&i915->drm, "msg_id: %d, ret: %d, timeout: %d\n",
			    msg_id, ret, timeout);

	return ret ? ret : msg_sz;
}

static
int intel_hdmi_hdcp2_write_msg(struct intel_connector *connector,
			       void *buf, size_t size)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	unsigned int offset;

	offset = HDCP_2_2_HDMI_REG_WR_MSG_OFFSET;
	return intel_hdmi_hdcp_write(dig_port, offset, buf, size);
}

static
int intel_hdmi_hdcp2_read_msg(struct intel_connector *connector,
			      u8 msg_id, void *buf, size_t size)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_hdmi *hdmi = &dig_port->hdmi;
	struct intel_hdcp *hdcp = &hdmi->attached_connector->hdcp;
	unsigned int offset;
	ssize_t ret;

	ret = intel_hdmi_hdcp2_wait_for_msg(dig_port, msg_id,
					    hdcp->is_paired);
	if (ret < 0)
		return ret;

	/*
	 * Available msg size should be equal to or lesser than the
	 * available buffer.
	 */
	if (ret > size) {
		drm_dbg_kms(&i915->drm,
			    "msg_sz(%zd) is more than exp size(%zu)\n",
			    ret, size);
		return -EINVAL;
	}

	offset = HDCP_2_2_HDMI_REG_RD_MSG_OFFSET;
	ret = intel_hdmi_hdcp_read(dig_port, offset, buf, ret);
	if (ret)
		drm_dbg_kms(&i915->drm, "Failed to read msg_id: %d(%zd)\n",
			    msg_id, ret);

	return ret;
}

static
int intel_hdmi_hdcp2_check_link(struct intel_digital_port *dig_port,
				struct intel_connector *connector)
{
	u8 rx_status[HDCP_2_2_HDMI_RXSTATUS_LEN];
	int ret;

	ret = intel_hdmi_hdcp2_read_rx_status(dig_port, rx_status);
	if (ret)
		return ret;

	/*
	 * Re-auth request and Link Integrity Failures are represented by
	 * same bit. i.e reauth_req.
	 */
	if (HDCP_2_2_HDMI_RXSTATUS_REAUTH_REQ(rx_status[1]))
		ret = HDCP_REAUTH_REQUEST;
	else if (HDCP_2_2_HDMI_RXSTATUS_READY(rx_status[1]))
		ret = HDCP_TOPOLOGY_CHANGE;

	return ret;
}

static
int intel_hdmi_hdcp2_capable(struct intel_connector *connector,
			     bool *capable)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	u8 hdcp2_version;
	int ret;

	*capable = false;
	ret = intel_hdmi_hdcp_read(dig_port, HDCP_2_2_HDMI_REG_VER_OFFSET,
				   &hdcp2_version, sizeof(hdcp2_version));
	if (!ret && hdcp2_version & HDCP_2_2_HDMI_SUPPORT_MASK)
		*capable = true;

	return ret;
}

static const struct intel_hdcp_shim intel_hdmi_hdcp_shim = {
	.write_an_aksv = intel_hdmi_hdcp_write_an_aksv,
	.read_bksv = intel_hdmi_hdcp_read_bksv,
	.read_bstatus = intel_hdmi_hdcp_read_bstatus,
	.repeater_present = intel_hdmi_hdcp_repeater_present,
	.read_ri_prime = intel_hdmi_hdcp_read_ri_prime,
	.read_ksv_ready = intel_hdmi_hdcp_read_ksv_ready,
	.read_ksv_fifo = intel_hdmi_hdcp_read_ksv_fifo,
	.read_v_prime_part = intel_hdmi_hdcp_read_v_prime_part,
	.toggle_signalling = intel_hdmi_hdcp_toggle_signalling,
	.check_link = intel_hdmi_hdcp_check_link,
	.write_2_2_msg = intel_hdmi_hdcp2_write_msg,
	.read_2_2_msg = intel_hdmi_hdcp2_read_msg,
	.check_2_2_link	= intel_hdmi_hdcp2_check_link,
	.hdcp_2_2_capable = intel_hdmi_hdcp2_capable,
	.protocol = HDCP_PROTOCOL_HDMI,
};

static int intel_hdmi_source_max_tmds_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int max_tmds_clock, vbt_max_tmds_clock;

	if (DISPLAY_VER(dev_priv) >= 10)
		max_tmds_clock = 594000;
	else if (DISPLAY_VER(dev_priv) >= 8 || IS_HASWELL(dev_priv))
		max_tmds_clock = 300000;
	else if (DISPLAY_VER(dev_priv) >= 5)
		max_tmds_clock = 225000;
	else
		max_tmds_clock = 165000;

	vbt_max_tmds_clock = intel_bios_hdmi_max_tmds_clock(encoder->devdata);
	if (vbt_max_tmds_clock)
		max_tmds_clock = min(max_tmds_clock, vbt_max_tmds_clock);

	return max_tmds_clock;
}

static bool intel_has_hdmi_sink(struct intel_hdmi *hdmi,
				const struct drm_connector_state *conn_state)
{
	struct intel_connector *connector = hdmi->attached_connector;

	return connector->base.display_info.is_hdmi &&
		READ_ONCE(to_intel_digital_connector_state(conn_state)->force_audio) != HDMI_AUDIO_OFF_DVI;
}

static bool intel_hdmi_is_ycbcr420(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420;
}

static int hdmi_port_clock_limit(struct intel_hdmi *hdmi,
				 bool respect_downstream_limits,
				 bool has_hdmi_sink)
{
	struct intel_encoder *encoder = &hdmi_to_dig_port(hdmi)->base;
	int max_tmds_clock = intel_hdmi_source_max_tmds_clock(encoder);

	if (respect_downstream_limits) {
		struct intel_connector *connector = hdmi->attached_connector;
		const struct drm_display_info *info = &connector->base.display_info;

		if (hdmi->dp_dual_mode.max_tmds_clock)
			max_tmds_clock = min(max_tmds_clock,
					     hdmi->dp_dual_mode.max_tmds_clock);

		if (info->max_tmds_clock)
			max_tmds_clock = min(max_tmds_clock,
					     info->max_tmds_clock);
		else if (!has_hdmi_sink)
			max_tmds_clock = min(max_tmds_clock, 165000);
	}

	return max_tmds_clock;
}

static enum drm_mode_status
hdmi_port_clock_valid(struct intel_hdmi *hdmi,
		      int clock, bool respect_downstream_limits,
		      bool has_hdmi_sink)
{
	struct drm_i915_private *dev_priv = intel_hdmi_to_i915(hdmi);
	enum phy phy = intel_port_to_phy(dev_priv, hdmi_to_dig_port(hdmi)->base.port);

	if (clock < 25000)
		return MODE_CLOCK_LOW;
	if (clock > hdmi_port_clock_limit(hdmi, respect_downstream_limits,
					  has_hdmi_sink))
		return MODE_CLOCK_HIGH;

	/* GLK DPLL can't generate 446-480 MHz */
	if (IS_GEMINILAKE(dev_priv) && clock > 446666 && clock < 480000)
		return MODE_CLOCK_RANGE;

	/* BXT/GLK DPLL can't generate 223-240 MHz */
	if ((IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv)) &&
	    clock > 223333 && clock < 240000)
		return MODE_CLOCK_RANGE;

	/* CHV DPLL can't generate 216-240 MHz */
	if (IS_CHERRYVIEW(dev_priv) && clock > 216000 && clock < 240000)
		return MODE_CLOCK_RANGE;

	/* ICL+ combo PHY PLL can't generate 500-533.2 MHz */
	if (intel_phy_is_combo(dev_priv, phy) && clock > 500000 && clock < 533200)
		return MODE_CLOCK_RANGE;

	/* ICL+ TC PHY PLL can't generate 500-532.8 MHz */
	if (intel_phy_is_tc(dev_priv, phy) && clock > 500000 && clock < 532800)
		return MODE_CLOCK_RANGE;

	/*
	 * SNPS PHYs' MPLLB table-based programming can only handle a fixed
	 * set of link rates.
	 *
	 * FIXME: We will hopefully get an algorithmic way of programming
	 * the MPLLB for HDMI in the future.
	 */
	if (DISPLAY_VER(dev_priv) >= 14)
		return intel_cx0_phy_check_hdmi_link_rate(hdmi, clock);
	else if (IS_DG2(dev_priv))
		return intel_snps_phy_check_hdmi_link_rate(clock);

	return MODE_OK;
}

int intel_hdmi_tmds_clock(int clock, int bpc,
			  enum intel_output_format sink_format)
{
	/* YCBCR420 TMDS rate requirement is half the pixel clock */
	if (sink_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		clock /= 2;

	/*
	 * Need to adjust the port link by:
	 *  1.5x for 12bpc
	 *  1.25x for 10bpc
	 */
	return DIV_ROUND_CLOSEST(clock * bpc, 8);
}

static bool intel_hdmi_source_bpc_possible(struct drm_i915_private *i915, int bpc)
{
	switch (bpc) {
	case 12:
		return !HAS_GMCH(i915);
	case 10:
		return DISPLAY_VER(i915) >= 11;
	case 8:
		return true;
	default:
		MISSING_CASE(bpc);
		return false;
	}
}

static bool intel_hdmi_sink_bpc_possible(struct drm_connector *connector,
					 int bpc, bool has_hdmi_sink,
					 enum intel_output_format sink_format)
{
	const struct drm_display_info *info = &connector->display_info;
	const struct drm_hdmi_info *hdmi = &info->hdmi;

	switch (bpc) {
	case 12:
		if (!has_hdmi_sink)
			return false;

		if (sink_format == INTEL_OUTPUT_FORMAT_YCBCR420)
			return hdmi->y420_dc_modes & DRM_EDID_YCBCR420_DC_36;
		else
			return info->edid_hdmi_rgb444_dc_modes & DRM_EDID_HDMI_DC_36;
	case 10:
		if (!has_hdmi_sink)
			return false;

		if (sink_format == INTEL_OUTPUT_FORMAT_YCBCR420)
			return hdmi->y420_dc_modes & DRM_EDID_YCBCR420_DC_30;
		else
			return info->edid_hdmi_rgb444_dc_modes & DRM_EDID_HDMI_DC_30;
	case 8:
		return true;
	default:
		MISSING_CASE(bpc);
		return false;
	}
}

static enum drm_mode_status
intel_hdmi_mode_clock_valid(struct drm_connector *connector, int clock,
			    bool has_hdmi_sink,
			    enum intel_output_format sink_format)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);
	struct intel_hdmi *hdmi = intel_attached_hdmi(to_intel_connector(connector));
	enum drm_mode_status status = MODE_OK;
	int bpc;

	/*
	 * Try all color depths since valid port clock range
	 * can have holes. Any mode that can be used with at
	 * least one color depth is accepted.
	 */
	for (bpc = 12; bpc >= 8; bpc -= 2) {
		int tmds_clock = intel_hdmi_tmds_clock(clock, bpc, sink_format);

		if (!intel_hdmi_source_bpc_possible(i915, bpc))
			continue;

		if (!intel_hdmi_sink_bpc_possible(connector, bpc, has_hdmi_sink, sink_format))
			continue;

		status = hdmi_port_clock_valid(hdmi, tmds_clock, true, has_hdmi_sink);
		if (status == MODE_OK)
			return MODE_OK;
	}

	/* can never happen */
	drm_WARN_ON(&i915->drm, status == MODE_OK);

	return status;
}

static enum drm_mode_status
intel_hdmi_mode_valid(struct drm_connector *connector,
		      struct drm_display_mode *mode)
{
	struct intel_hdmi *hdmi = intel_attached_hdmi(to_intel_connector(connector));
	struct drm_i915_private *dev_priv = intel_hdmi_to_i915(hdmi);
	enum drm_mode_status status;
	int clock = mode->clock;
	int max_dotclk = to_i915(connector->dev)->max_dotclk_freq;
	bool has_hdmi_sink = intel_has_hdmi_sink(hdmi, connector->state);
	bool ycbcr_420_only;
	enum intel_output_format sink_format;

	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) == DRM_MODE_FLAG_3D_FRAME_PACKING)
		clock *= 2;

	if (clock > max_dotclk)
		return MODE_CLOCK_HIGH;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK) {
		if (!has_hdmi_sink)
			return MODE_CLOCK_LOW;
		clock *= 2;
	}

	/*
	 * HDMI2.1 requires higher resolution modes like 8k60, 4K120 to be
	 * enumerated only if FRL is supported. Current platforms do not support
	 * FRL so prune the higher resolution modes that require doctclock more
	 * than 600MHz.
	 */
	if (clock > 600000)
		return MODE_CLOCK_HIGH;

	ycbcr_420_only = drm_mode_is_420_only(&connector->display_info, mode);

	if (ycbcr_420_only)
		sink_format = INTEL_OUTPUT_FORMAT_YCBCR420;
	else
		sink_format = INTEL_OUTPUT_FORMAT_RGB;

	status = intel_hdmi_mode_clock_valid(connector, clock, has_hdmi_sink, sink_format);
	if (status != MODE_OK) {
		if (ycbcr_420_only ||
		    !connector->ycbcr_420_allowed ||
		    !drm_mode_is_420_also(&connector->display_info, mode))
			return status;

		sink_format = INTEL_OUTPUT_FORMAT_YCBCR420;
		status = intel_hdmi_mode_clock_valid(connector, clock, has_hdmi_sink, sink_format);
		if (status != MODE_OK)
			return status;
	}

	return intel_mode_valid_max_plane_size(dev_priv, mode, false);
}

bool intel_hdmi_bpc_possible(const struct intel_crtc_state *crtc_state,
			     int bpc, bool has_hdmi_sink)
{
	struct drm_atomic_state *state = crtc_state->uapi.state;
	struct drm_connector_state *connector_state;
	struct drm_connector *connector;
	int i;

	for_each_new_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != crtc_state->uapi.crtc)
			continue;

		if (!intel_hdmi_sink_bpc_possible(connector, bpc, has_hdmi_sink,
						  crtc_state->sink_format))
			return false;
	}

	return true;
}

static bool hdmi_bpc_possible(const struct intel_crtc_state *crtc_state, int bpc)
{
	struct drm_i915_private *dev_priv =
		to_i915(crtc_state->uapi.crtc->dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	if (!intel_hdmi_source_bpc_possible(dev_priv, bpc))
		return false;

	/* Display Wa_1405510057:icl,ehl */
	if (intel_hdmi_is_ycbcr420(crtc_state) &&
	    bpc == 10 && DISPLAY_VER(dev_priv) == 11 &&
	    (adjusted_mode->crtc_hblank_end -
	     adjusted_mode->crtc_hblank_start) % 8 == 2)
		return false;

	return intel_hdmi_bpc_possible(crtc_state, bpc, crtc_state->has_hdmi_sink);
}

static int intel_hdmi_compute_bpc(struct intel_encoder *encoder,
				  struct intel_crtc_state *crtc_state,
				  int clock, bool respect_downstream_limits)
{
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);
	int bpc;

	/*
	 * pipe_bpp could already be below 8bpc due to FDI
	 * bandwidth constraints. HDMI minimum is 8bpc however.
	 */
	bpc = max(crtc_state->pipe_bpp / 3, 8);

	/*
	 * We will never exceed downstream TMDS clock limits while
	 * attempting deep color. If the user insists on forcing an
	 * out of spec mode they will have to be satisfied with 8bpc.
	 */
	if (!respect_downstream_limits)
		bpc = 8;

	for (; bpc >= 8; bpc -= 2) {
		int tmds_clock = intel_hdmi_tmds_clock(clock, bpc,
						       crtc_state->sink_format);

		if (hdmi_bpc_possible(crtc_state, bpc) &&
		    hdmi_port_clock_valid(intel_hdmi, tmds_clock,
					  respect_downstream_limits,
					  crtc_state->has_hdmi_sink) == MODE_OK)
			return bpc;
	}

	return -EINVAL;
}

static int intel_hdmi_compute_clock(struct intel_encoder *encoder,
				    struct intel_crtc_state *crtc_state,
				    bool respect_downstream_limits)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int bpc, clock = adjusted_mode->crtc_clock;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLCLK)
		clock *= 2;

	bpc = intel_hdmi_compute_bpc(encoder, crtc_state, clock,
				     respect_downstream_limits);
	if (bpc < 0)
		return bpc;

	crtc_state->port_clock =
		intel_hdmi_tmds_clock(clock, bpc, crtc_state->sink_format);

	/*
	 * pipe_bpp could already be below 8bpc due to
	 * FDI bandwidth constraints. We shouldn't bump it
	 * back up to the HDMI minimum 8bpc in that case.
	 */
	crtc_state->pipe_bpp = min(crtc_state->pipe_bpp, bpc * 3);

	drm_dbg_kms(&i915->drm,
		    "picking %d bpc for HDMI output (pipe bpp: %d)\n",
		    bpc, crtc_state->pipe_bpp);

	return 0;
}

bool intel_hdmi_limited_color_range(const struct intel_crtc_state *crtc_state,
				    const struct drm_connector_state *conn_state)
{
	const struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(conn_state);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	/*
	 * Our YCbCr output is always limited range.
	 * crtc_state->limited_color_range only applies to RGB,
	 * and it must never be set for YCbCr or we risk setting
	 * some conflicting bits in TRANSCONF which will mess up
	 * the colors on the monitor.
	 */
	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB)
		return false;

	if (intel_conn_state->broadcast_rgb == INTEL_BROADCAST_RGB_AUTO) {
		/* See CEA-861-E - 5.1 Default Encoding Parameters */
		return crtc_state->has_hdmi_sink &&
			drm_default_rgb_quant_range(adjusted_mode) ==
			HDMI_QUANTIZATION_RANGE_LIMITED;
	} else {
		return intel_conn_state->broadcast_rgb == INTEL_BROADCAST_RGB_LIMITED;
	}
}

static bool intel_hdmi_has_audio(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state,
				 const struct drm_connector_state *conn_state)
{
	struct drm_connector *connector = conn_state->connector;
	const struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(conn_state);

	if (!crtc_state->has_hdmi_sink)
		return false;

	if (intel_conn_state->force_audio == HDMI_AUDIO_AUTO)
		return connector->display_info.has_audio;
	else
		return intel_conn_state->force_audio == HDMI_AUDIO_ON;
}

static enum intel_output_format
intel_hdmi_sink_format(const struct intel_crtc_state *crtc_state,
		       struct intel_connector *connector,
		       bool ycbcr_420_output)
{
	if (!crtc_state->has_hdmi_sink)
		return INTEL_OUTPUT_FORMAT_RGB;

	if (connector->base.ycbcr_420_allowed && ycbcr_420_output)
		return INTEL_OUTPUT_FORMAT_YCBCR420;
	else
		return INTEL_OUTPUT_FORMAT_RGB;
}

static enum intel_output_format
intel_hdmi_output_format(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->sink_format;
}

static int intel_hdmi_compute_output_format(struct intel_encoder *encoder,
					    struct intel_crtc_state *crtc_state,
					    const struct drm_connector_state *conn_state,
					    bool respect_downstream_limits)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	const struct drm_display_info *info = &connector->base.display_info;
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	bool ycbcr_420_only = drm_mode_is_420_only(info, adjusted_mode);
	int ret;

	crtc_state->sink_format =
		intel_hdmi_sink_format(crtc_state, connector, ycbcr_420_only);

	if (ycbcr_420_only && crtc_state->sink_format != INTEL_OUTPUT_FORMAT_YCBCR420) {
		drm_dbg_kms(&i915->drm,
			    "YCbCr 4:2:0 mode but YCbCr 4:2:0 output not possible. Falling back to RGB.\n");
		crtc_state->sink_format = INTEL_OUTPUT_FORMAT_RGB;
	}

	crtc_state->output_format = intel_hdmi_output_format(crtc_state);
	ret = intel_hdmi_compute_clock(encoder, crtc_state, respect_downstream_limits);
	if (ret) {
		if (crtc_state->sink_format == INTEL_OUTPUT_FORMAT_YCBCR420 ||
		    !crtc_state->has_hdmi_sink ||
		    !connector->base.ycbcr_420_allowed ||
		    !drm_mode_is_420_also(info, adjusted_mode))
			return ret;

		crtc_state->sink_format = INTEL_OUTPUT_FORMAT_YCBCR420;
		crtc_state->output_format = intel_hdmi_output_format(crtc_state);
		ret = intel_hdmi_compute_clock(encoder, crtc_state, respect_downstream_limits);
	}

	return ret;
}

static bool intel_hdmi_is_cloned(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->uapi.encoder_mask &&
		!is_power_of_2(crtc_state->uapi.encoder_mask);
}

static bool source_supports_scrambling(struct intel_encoder *encoder)
{
	/*
	 * Gen 10+ support HDMI 2.0 : the max tmds clock is 594MHz, and
	 * scrambling is supported.
	 * But there seem to be cases where certain platforms that support
	 * HDMI 2.0, have an HDMI1.4 retimer chip, and the max tmds clock is
	 * capped by VBT to less than 340MHz.
	 *
	 * In such cases when an HDMI2.0 sink is connected, it creates a
	 * problem : the platform and the sink both support scrambling but the
	 * HDMI 1.4 retimer chip doesn't.
	 *
	 * So go for scrambling, based on the max tmds clock taking into account,
	 * restrictions coming from VBT.
	 */
	return intel_hdmi_source_max_tmds_clock(encoder) > 340000;
}

bool intel_hdmi_compute_has_hdmi_sink(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state,
				      const struct drm_connector_state *conn_state)
{
	struct intel_hdmi *hdmi = enc_to_intel_hdmi(encoder);

	return intel_has_hdmi_sink(hdmi, conn_state) &&
		!intel_hdmi_is_cloned(crtc_state);
}

int intel_hdmi_compute_config(struct intel_encoder *encoder,
			      struct intel_crtc_state *pipe_config,
			      struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	struct drm_connector *connector = conn_state->connector;
	struct drm_scdc *scdc = &connector->display_info.hdmi.scdc;
	int ret;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	if (!connector->interlace_allowed &&
	    adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		return -EINVAL;

	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;

	if (pipe_config->has_hdmi_sink)
		pipe_config->has_infoframe = true;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLCLK)
		pipe_config->pixel_multiplier = 2;

	pipe_config->has_audio =
		intel_hdmi_has_audio(encoder, pipe_config, conn_state) &&
		intel_audio_compute_config(encoder, pipe_config, conn_state);

	/*
	 * Try to respect downstream TMDS clock limits first, if
	 * that fails assume the user might know something we don't.
	 */
	ret = intel_hdmi_compute_output_format(encoder, pipe_config, conn_state, true);
	if (ret)
		ret = intel_hdmi_compute_output_format(encoder, pipe_config, conn_state, false);
	if (ret) {
		drm_dbg_kms(&dev_priv->drm,
			    "unsupported HDMI clock (%d kHz), rejecting mode\n",
			    pipe_config->hw.adjusted_mode.crtc_clock);
		return ret;
	}

	if (intel_hdmi_is_ycbcr420(pipe_config)) {
		ret = intel_panel_fitting(pipe_config, conn_state);
		if (ret)
			return ret;
	}

	pipe_config->limited_color_range =
		intel_hdmi_limited_color_range(pipe_config, conn_state);

	if (conn_state->picture_aspect_ratio)
		adjusted_mode->picture_aspect_ratio =
			conn_state->picture_aspect_ratio;

	pipe_config->lane_count = 4;

	if (scdc->scrambling.supported && source_supports_scrambling(encoder)) {
		if (scdc->scrambling.low_rates)
			pipe_config->hdmi_scrambling = true;

		if (pipe_config->port_clock > 340000) {
			pipe_config->hdmi_scrambling = true;
			pipe_config->hdmi_high_tmds_clock_ratio = true;
		}
	}

	intel_hdmi_compute_gcp_infoframe(encoder, pipe_config,
					 conn_state);

	if (!intel_hdmi_compute_avi_infoframe(encoder, pipe_config, conn_state)) {
		drm_dbg_kms(&dev_priv->drm, "bad AVI infoframe\n");
		return -EINVAL;
	}

	if (!intel_hdmi_compute_spd_infoframe(encoder, pipe_config, conn_state)) {
		drm_dbg_kms(&dev_priv->drm, "bad SPD infoframe\n");
		return -EINVAL;
	}

	if (!intel_hdmi_compute_hdmi_infoframe(encoder, pipe_config, conn_state)) {
		drm_dbg_kms(&dev_priv->drm, "bad HDMI infoframe\n");
		return -EINVAL;
	}

	if (!intel_hdmi_compute_drm_infoframe(encoder, pipe_config, conn_state)) {
		drm_dbg_kms(&dev_priv->drm, "bad DRM infoframe\n");
		return -EINVAL;
	}

	return 0;
}

void intel_hdmi_encoder_shutdown(struct intel_encoder *encoder)
{
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);

	/*
	 * Give a hand to buggy BIOSen which forget to turn
	 * the TMDS output buffers back on after a reboot.
	 */
	intel_dp_dual_mode_set_tmds_output(intel_hdmi, true);
}

static void
intel_hdmi_unset_edid(struct drm_connector *connector)
{
	struct intel_hdmi *intel_hdmi = intel_attached_hdmi(to_intel_connector(connector));

	intel_hdmi->dp_dual_mode.type = DRM_DP_DUAL_MODE_NONE;
	intel_hdmi->dp_dual_mode.max_tmds_clock = 0;

	drm_edid_free(to_intel_connector(connector)->detect_edid);
	to_intel_connector(connector)->detect_edid = NULL;
}

static void
intel_hdmi_dp_dual_mode_detect(struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_hdmi *hdmi = intel_attached_hdmi(to_intel_connector(connector));
	struct intel_encoder *encoder = &hdmi_to_dig_port(hdmi)->base;
	struct i2c_adapter *ddc = connector->ddc;
	enum drm_dp_dual_mode_type type;

	type = drm_dp_dual_mode_detect(&dev_priv->drm, ddc);

	/*
	 * Type 1 DVI adaptors are not required to implement any
	 * registers, so we can't always detect their presence.
	 * Ideally we should be able to check the state of the
	 * CONFIG1 pin, but no such luck on our hardware.
	 *
	 * The only method left to us is to check the VBT to see
	 * if the port is a dual mode capable DP port.
	 */
	if (type == DRM_DP_DUAL_MODE_UNKNOWN) {
		if (!connector->force &&
		    intel_bios_encoder_supports_dp_dual_mode(encoder->devdata)) {
			drm_dbg_kms(&dev_priv->drm,
				    "Assuming DP dual mode adaptor presence based on VBT\n");
			type = DRM_DP_DUAL_MODE_TYPE1_DVI;
		} else {
			type = DRM_DP_DUAL_MODE_NONE;
		}
	}

	if (type == DRM_DP_DUAL_MODE_NONE)
		return;

	hdmi->dp_dual_mode.type = type;
	hdmi->dp_dual_mode.max_tmds_clock =
		drm_dp_dual_mode_max_tmds_clock(&dev_priv->drm, type, ddc);

	drm_dbg_kms(&dev_priv->drm,
		    "DP dual mode adaptor (%s) detected (max TMDS clock: %d kHz)\n",
		    drm_dp_get_dual_mode_type_name(type),
		    hdmi->dp_dual_mode.max_tmds_clock);

	/* Older VBTs are often buggy and can't be trusted :( Play it safe. */
	if ((DISPLAY_VER(dev_priv) >= 8 || IS_HASWELL(dev_priv)) &&
	    !intel_bios_encoder_supports_dp_dual_mode(encoder->devdata)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Ignoring DP dual mode adaptor max TMDS clock for native HDMI port\n");
		hdmi->dp_dual_mode.max_tmds_clock = 0;
	}
}

static bool
intel_hdmi_set_edid(struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_hdmi *intel_hdmi = intel_attached_hdmi(to_intel_connector(connector));
	struct i2c_adapter *ddc = connector->ddc;
	intel_wakeref_t wakeref;
	const struct drm_edid *drm_edid;
	bool connected = false;

	wakeref = intel_display_power_get(dev_priv, POWER_DOMAIN_GMBUS);

	drm_edid = drm_edid_read_ddc(connector, ddc);

	if (!drm_edid && !intel_gmbus_is_forced_bit(ddc)) {
		drm_dbg_kms(&dev_priv->drm,
			    "HDMI GMBUS EDID read failed, retry using GPIO bit-banging\n");
		intel_gmbus_force_bit(ddc, true);
		drm_edid = drm_edid_read_ddc(connector, ddc);
		intel_gmbus_force_bit(ddc, false);
	}

	/* Below we depend on display info having been updated */
	drm_edid_connector_update(connector, drm_edid);

	to_intel_connector(connector)->detect_edid = drm_edid;

	if (drm_edid_is_digital(drm_edid)) {
		intel_hdmi_dp_dual_mode_detect(connector);

		connected = true;
	}

	intel_display_power_put(dev_priv, POWER_DOMAIN_GMBUS, wakeref);

	cec_notifier_set_phys_addr(intel_hdmi->cec_notifier,
				   connector->display_info.source_physical_address);

	return connected;
}

static enum drm_connector_status
intel_hdmi_detect(struct drm_connector *connector, bool force)
{
	enum drm_connector_status status = connector_status_disconnected;
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_hdmi *intel_hdmi = intel_attached_hdmi(to_intel_connector(connector));
	struct intel_encoder *encoder = &hdmi_to_dig_port(intel_hdmi)->base;
	intel_wakeref_t wakeref;

	drm_dbg_kms(&dev_priv->drm, "[CONNECTOR:%d:%s]\n",
		    connector->base.id, connector->name);

	if (!intel_display_device_enabled(dev_priv))
		return connector_status_disconnected;

	wakeref = intel_display_power_get(dev_priv, POWER_DOMAIN_GMBUS);

	if (DISPLAY_VER(dev_priv) >= 11 &&
	    !intel_digital_port_connected(encoder))
		goto out;

	intel_hdmi_unset_edid(connector);

	if (intel_hdmi_set_edid(connector))
		status = connector_status_connected;

out:
	intel_display_power_put(dev_priv, POWER_DOMAIN_GMBUS, wakeref);

	if (status != connector_status_connected)
		cec_notifier_phys_addr_invalidate(intel_hdmi->cec_notifier);

	return status;
}

static void
intel_hdmi_force(struct drm_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);

	drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s]\n",
		    connector->base.id, connector->name);

	intel_hdmi_unset_edid(connector);

	if (connector->status != connector_status_connected)
		return;

	intel_hdmi_set_edid(connector);
}

static int intel_hdmi_get_modes(struct drm_connector *connector)
{
	/* drm_edid_connector_update() done in ->detect() or ->force() */
	return drm_edid_connector_add_modes(connector);
}

static int
intel_hdmi_connector_register(struct drm_connector *connector)
{
	int ret;

	ret = intel_connector_register(connector);
	if (ret)
		return ret;

	return ret;
}

static void intel_hdmi_connector_unregister(struct drm_connector *connector)
{
	struct cec_notifier *n = intel_attached_hdmi(to_intel_connector(connector))->cec_notifier;

	cec_notifier_conn_unregister(n);

	intel_connector_unregister(connector);
}

static const struct drm_connector_funcs intel_hdmi_connector_funcs = {
	.detect = intel_hdmi_detect,
	.force = intel_hdmi_force,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_get_property = intel_digital_connector_atomic_get_property,
	.atomic_set_property = intel_digital_connector_atomic_set_property,
	.late_register = intel_hdmi_connector_register,
	.early_unregister = intel_hdmi_connector_unregister,
	.destroy = intel_connector_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_digital_connector_duplicate_state,
};

static int intel_hdmi_connector_atomic_check(struct drm_connector *connector,
					     struct drm_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->dev);

	if (HAS_DDI(i915))
		return intel_digital_connector_atomic_check(connector, state);
	else
		return g4x_hdmi_connector_atomic_check(connector, state);
}

static const struct drm_connector_helper_funcs intel_hdmi_connector_helper_funcs = {
	.get_modes = intel_hdmi_get_modes,
	.mode_valid = intel_hdmi_mode_valid,
	.atomic_check = intel_hdmi_connector_atomic_check,
};

static void
intel_hdmi_add_properties(struct intel_hdmi *intel_hdmi, struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);

	intel_attach_force_audio_property(connector);
	intel_attach_broadcast_rgb_property(connector);
	intel_attach_aspect_ratio_property(connector);

	intel_attach_hdmi_colorspace_property(connector);
	drm_connector_attach_content_type_property(connector);

	if (DISPLAY_VER(dev_priv) >= 10)
		drm_connector_attach_hdr_output_metadata_property(connector);

	if (!HAS_GMCH(dev_priv))
		drm_connector_attach_max_bpc_property(connector, 8, 12);
}

/*
 * intel_hdmi_handle_sink_scrambling: handle sink scrambling/clock ratio setup
 * @encoder: intel_encoder
 * @connector: drm_connector
 * @high_tmds_clock_ratio = bool to indicate if the function needs to set
 *  or reset the high tmds clock ratio for scrambling
 * @scrambling: bool to Indicate if the function needs to set or reset
 *  sink scrambling
 *
 * This function handles scrambling on HDMI 2.0 capable sinks.
 * If required clock rate is > 340 Mhz && scrambling is supported by sink
 * it enables scrambling. This should be called before enabling the HDMI
 * 2.0 port, as the sink can choose to disable the scrambling if it doesn't
 * detect a scrambled clock within 100 ms.
 *
 * Returns:
 * True on success, false on failure.
 */
bool intel_hdmi_handle_sink_scrambling(struct intel_encoder *encoder,
				       struct drm_connector *connector,
				       bool high_tmds_clock_ratio,
				       bool scrambling)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_scrambling *sink_scrambling =
		&connector->display_info.hdmi.scdc.scrambling;

	if (!sink_scrambling->supported)
		return true;

	drm_dbg_kms(&dev_priv->drm,
		    "[CONNECTOR:%d:%s] scrambling=%s, TMDS bit clock ratio=1/%d\n",
		    connector->base.id, connector->name,
		    str_yes_no(scrambling), high_tmds_clock_ratio ? 40 : 10);

	/* Set TMDS bit clock ratio to 1/40 or 1/10, and enable/disable scrambling */
	return drm_scdc_set_high_tmds_clock_ratio(connector, high_tmds_clock_ratio) &&
		drm_scdc_set_scrambling(connector, scrambling);
}

static u8 chv_port_to_ddc_pin(struct drm_i915_private *dev_priv, enum port port)
{
	u8 ddc_pin;

	switch (port) {
	case PORT_B:
		ddc_pin = GMBUS_PIN_DPB;
		break;
	case PORT_C:
		ddc_pin = GMBUS_PIN_DPC;
		break;
	case PORT_D:
		ddc_pin = GMBUS_PIN_DPD_CHV;
		break;
	default:
		MISSING_CASE(port);
		ddc_pin = GMBUS_PIN_DPB;
		break;
	}
	return ddc_pin;
}

static u8 bxt_port_to_ddc_pin(struct drm_i915_private *dev_priv, enum port port)
{
	u8 ddc_pin;

	switch (port) {
	case PORT_B:
		ddc_pin = GMBUS_PIN_1_BXT;
		break;
	case PORT_C:
		ddc_pin = GMBUS_PIN_2_BXT;
		break;
	default:
		MISSING_CASE(port);
		ddc_pin = GMBUS_PIN_1_BXT;
		break;
	}
	return ddc_pin;
}

static u8 cnp_port_to_ddc_pin(struct drm_i915_private *dev_priv,
			      enum port port)
{
	u8 ddc_pin;

	switch (port) {
	case PORT_B:
		ddc_pin = GMBUS_PIN_1_BXT;
		break;
	case PORT_C:
		ddc_pin = GMBUS_PIN_2_BXT;
		break;
	case PORT_D:
		ddc_pin = GMBUS_PIN_4_CNP;
		break;
	case PORT_F:
		ddc_pin = GMBUS_PIN_3_BXT;
		break;
	default:
		MISSING_CASE(port);
		ddc_pin = GMBUS_PIN_1_BXT;
		break;
	}
	return ddc_pin;
}

static u8 icl_port_to_ddc_pin(struct drm_i915_private *dev_priv, enum port port)
{
	enum phy phy = intel_port_to_phy(dev_priv, port);

	if (intel_phy_is_combo(dev_priv, phy))
		return GMBUS_PIN_1_BXT + port;
	else if (intel_phy_is_tc(dev_priv, phy))
		return GMBUS_PIN_9_TC1_ICP + intel_port_to_tc(dev_priv, port);

	drm_WARN(&dev_priv->drm, 1, "Unknown port:%c\n", port_name(port));
	return GMBUS_PIN_2_BXT;
}

static u8 mcc_port_to_ddc_pin(struct drm_i915_private *dev_priv, enum port port)
{
	enum phy phy = intel_port_to_phy(dev_priv, port);
	u8 ddc_pin;

	switch (phy) {
	case PHY_A:
		ddc_pin = GMBUS_PIN_1_BXT;
		break;
	case PHY_B:
		ddc_pin = GMBUS_PIN_2_BXT;
		break;
	case PHY_C:
		ddc_pin = GMBUS_PIN_9_TC1_ICP;
		break;
	default:
		MISSING_CASE(phy);
		ddc_pin = GMBUS_PIN_1_BXT;
		break;
	}
	return ddc_pin;
}

static u8 rkl_port_to_ddc_pin(struct drm_i915_private *dev_priv, enum port port)
{
	enum phy phy = intel_port_to_phy(dev_priv, port);

	WARN_ON(port == PORT_C);

	/*
	 * Pin mapping for RKL depends on which PCH is present.  With TGP, the
	 * final two outputs use type-c pins, even though they're actually
	 * combo outputs.  With CMP, the traditional DDI A-D pins are used for
	 * all outputs.
	 */
	if (INTEL_PCH_TYPE(dev_priv) >= PCH_TGP && phy >= PHY_C)
		return GMBUS_PIN_9_TC1_ICP + phy - PHY_C;

	return GMBUS_PIN_1_BXT + phy;
}

static u8 gen9bc_tgp_port_to_ddc_pin(struct drm_i915_private *i915, enum port port)
{
	enum phy phy = intel_port_to_phy(i915, port);

	drm_WARN_ON(&i915->drm, port == PORT_A);

	/*
	 * Pin mapping for GEN9 BC depends on which PCH is present.  With TGP,
	 * final two outputs use type-c pins, even though they're actually
	 * combo outputs.  With CMP, the traditional DDI A-D pins are used for
	 * all outputs.
	 */
	if (INTEL_PCH_TYPE(i915) >= PCH_TGP && phy >= PHY_C)
		return GMBUS_PIN_9_TC1_ICP + phy - PHY_C;

	return GMBUS_PIN_1_BXT + phy;
}

static u8 dg1_port_to_ddc_pin(struct drm_i915_private *dev_priv, enum port port)
{
	return intel_port_to_phy(dev_priv, port) + 1;
}

static u8 adls_port_to_ddc_pin(struct drm_i915_private *dev_priv, enum port port)
{
	enum phy phy = intel_port_to_phy(dev_priv, port);

	WARN_ON(port == PORT_B || port == PORT_C);

	/*
	 * Pin mapping for ADL-S requires TC pins for all combo phy outputs
	 * except first combo output.
	 */
	if (phy == PHY_A)
		return GMBUS_PIN_1_BXT;

	return GMBUS_PIN_9_TC1_ICP + phy - PHY_B;
}

static u8 g4x_port_to_ddc_pin(struct drm_i915_private *dev_priv,
			      enum port port)
{
	u8 ddc_pin;

	switch (port) {
	case PORT_B:
		ddc_pin = GMBUS_PIN_DPB;
		break;
	case PORT_C:
		ddc_pin = GMBUS_PIN_DPC;
		break;
	case PORT_D:
		ddc_pin = GMBUS_PIN_DPD;
		break;
	default:
		MISSING_CASE(port);
		ddc_pin = GMBUS_PIN_DPB;
		break;
	}
	return ddc_pin;
}

static u8 intel_hdmi_default_ddc_pin(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	u8 ddc_pin;

	if (IS_ALDERLAKE_S(dev_priv))
		ddc_pin = adls_port_to_ddc_pin(dev_priv, port);
	else if (INTEL_PCH_TYPE(dev_priv) >= PCH_DG1)
		ddc_pin = dg1_port_to_ddc_pin(dev_priv, port);
	else if (IS_ROCKETLAKE(dev_priv))
		ddc_pin = rkl_port_to_ddc_pin(dev_priv, port);
	else if (DISPLAY_VER(dev_priv) == 9 && HAS_PCH_TGP(dev_priv))
		ddc_pin = gen9bc_tgp_port_to_ddc_pin(dev_priv, port);
	else if ((IS_JASPERLAKE(dev_priv) || IS_ELKHARTLAKE(dev_priv)) &&
		 HAS_PCH_TGP(dev_priv))
		ddc_pin = mcc_port_to_ddc_pin(dev_priv, port);
	else if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		ddc_pin = icl_port_to_ddc_pin(dev_priv, port);
	else if (HAS_PCH_CNP(dev_priv))
		ddc_pin = cnp_port_to_ddc_pin(dev_priv, port);
	else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		ddc_pin = bxt_port_to_ddc_pin(dev_priv, port);
	else if (IS_CHERRYVIEW(dev_priv))
		ddc_pin = chv_port_to_ddc_pin(dev_priv, port);
	else
		ddc_pin = g4x_port_to_ddc_pin(dev_priv, port);

	return ddc_pin;
}

static struct intel_encoder *
get_encoder_by_ddc_pin(struct intel_encoder *encoder, u8 ddc_pin)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_encoder *other;

	for_each_intel_encoder(&i915->drm, other) {
		struct intel_connector *connector;

		if (other == encoder)
			continue;

		if (!intel_encoder_is_dig_port(other))
			continue;

		connector = enc_to_dig_port(other)->hdmi.attached_connector;

		if (connector && connector->base.ddc == intel_gmbus_get_adapter(i915, ddc_pin))
			return other;
	}

	return NULL;
}

static u8 intel_hdmi_ddc_pin(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_encoder *other;
	const char *source;
	u8 ddc_pin;

	ddc_pin = intel_bios_hdmi_ddc_pin(encoder->devdata);
	source = "VBT";

	if (!ddc_pin) {
		ddc_pin = intel_hdmi_default_ddc_pin(encoder);
		source = "platform default";
	}

	if (!intel_gmbus_is_valid_pin(i915, ddc_pin)) {
		drm_dbg_kms(&i915->drm, "[ENCODER:%d:%s] Invalid DDC pin %d\n",
			    encoder->base.base.id, encoder->base.name, ddc_pin);
		return 0;
	}

	other = get_encoder_by_ddc_pin(encoder, ddc_pin);
	if (other) {
		drm_dbg_kms(&i915->drm, "[ENCODER:%d:%s] DDC pin %d already claimed by [ENCODER:%d:%s]\n",
			    encoder->base.base.id, encoder->base.name, ddc_pin,
			    other->base.base.id, other->base.name);
		return 0;
	}

	drm_dbg_kms(&i915->drm,
		    "[ENCODER:%d:%s] Using DDC pin 0x%x (%s)\n",
		    encoder->base.base.id, encoder->base.name,
		    ddc_pin, source);

	return ddc_pin;
}

void intel_infoframe_init(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv =
		to_i915(dig_port->base.base.dev);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		dig_port->write_infoframe = vlv_write_infoframe;
		dig_port->read_infoframe = vlv_read_infoframe;
		dig_port->set_infoframes = vlv_set_infoframes;
		dig_port->infoframes_enabled = vlv_infoframes_enabled;
	} else if (IS_G4X(dev_priv)) {
		dig_port->write_infoframe = g4x_write_infoframe;
		dig_port->read_infoframe = g4x_read_infoframe;
		dig_port->set_infoframes = g4x_set_infoframes;
		dig_port->infoframes_enabled = g4x_infoframes_enabled;
	} else if (HAS_DDI(dev_priv)) {
		if (intel_bios_encoder_is_lspcon(dig_port->base.devdata)) {
			dig_port->write_infoframe = lspcon_write_infoframe;
			dig_port->read_infoframe = lspcon_read_infoframe;
			dig_port->set_infoframes = lspcon_set_infoframes;
			dig_port->infoframes_enabled = lspcon_infoframes_enabled;
		} else {
			dig_port->write_infoframe = hsw_write_infoframe;
			dig_port->read_infoframe = hsw_read_infoframe;
			dig_port->set_infoframes = hsw_set_infoframes;
			dig_port->infoframes_enabled = hsw_infoframes_enabled;
		}
	} else if (HAS_PCH_IBX(dev_priv)) {
		dig_port->write_infoframe = ibx_write_infoframe;
		dig_port->read_infoframe = ibx_read_infoframe;
		dig_port->set_infoframes = ibx_set_infoframes;
		dig_port->infoframes_enabled = ibx_infoframes_enabled;
	} else {
		dig_port->write_infoframe = cpt_write_infoframe;
		dig_port->read_infoframe = cpt_read_infoframe;
		dig_port->set_infoframes = cpt_set_infoframes;
		dig_port->infoframes_enabled = cpt_infoframes_enabled;
	}
}

void intel_hdmi_init_connector(struct intel_digital_port *dig_port,
			       struct intel_connector *intel_connector)
{
	struct drm_connector *connector = &intel_connector->base;
	struct intel_hdmi *intel_hdmi = &dig_port->hdmi;
	struct intel_encoder *intel_encoder = &dig_port->base;
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = intel_encoder->port;
	struct cec_connector_info conn_info;
	u8 ddc_pin;

	drm_dbg_kms(&dev_priv->drm,
		    "Adding HDMI connector on [ENCODER:%d:%s]\n",
		    intel_encoder->base.base.id, intel_encoder->base.name);

	if (DISPLAY_VER(dev_priv) < 12 && drm_WARN_ON(dev, port == PORT_A))
		return;

	if (drm_WARN(dev, dig_port->max_lanes < 4,
		     "Not enough lanes (%d) for HDMI on [ENCODER:%d:%s]\n",
		     dig_port->max_lanes, intel_encoder->base.base.id,
		     intel_encoder->base.name))
		return;

	ddc_pin = intel_hdmi_ddc_pin(intel_encoder);
	if (!ddc_pin)
		return;

	drm_connector_init_with_ddc(dev, connector,
				    &intel_hdmi_connector_funcs,
				    DRM_MODE_CONNECTOR_HDMIA,
				    intel_gmbus_get_adapter(dev_priv, ddc_pin));

	drm_connector_helper_add(connector, &intel_hdmi_connector_helper_funcs);

	if (DISPLAY_VER(dev_priv) < 12)
		connector->interlace_allowed = true;

	connector->stereo_allowed = true;

	if (DISPLAY_VER(dev_priv) >= 10)
		connector->ycbcr_420_allowed = true;

	intel_connector->polled = DRM_CONNECTOR_POLL_HPD;

	if (HAS_DDI(dev_priv))
		intel_connector->get_hw_state = intel_ddi_connector_get_hw_state;
	else
		intel_connector->get_hw_state = intel_connector_get_hw_state;

	intel_hdmi_add_properties(intel_hdmi, connector);

	intel_connector_attach_encoder(intel_connector, intel_encoder);
	intel_hdmi->attached_connector = intel_connector;

	if (is_hdcp_supported(dev_priv, port)) {
		int ret = intel_hdcp_init(intel_connector, dig_port,
					  &intel_hdmi_hdcp_shim);
		if (ret)
			drm_dbg_kms(&dev_priv->drm,
				    "HDCP init failed, skipping.\n");
	}

	/* For G4X desktop chip, PEG_BAND_GAP_DATA 3:0 must first be written
	 * 0xd.  Failure to do so will result in spurious interrupts being
	 * generated on the port when a cable is not attached.
	 */
	if (IS_G45(dev_priv)) {
		u32 temp = intel_de_read(dev_priv, PEG_BAND_GAP_DATA);
		intel_de_write(dev_priv, PEG_BAND_GAP_DATA,
		               (temp & ~0xf) | 0xd);
	}

	cec_fill_conn_info_from_drm(&conn_info, connector);

	intel_hdmi->cec_notifier =
		cec_notifier_conn_register(dev->dev, port_identifier(port),
					   &conn_info);
	if (!intel_hdmi->cec_notifier)
		drm_dbg_kms(&dev_priv->drm, "CEC notifier get failed\n");
}

/*
 * intel_hdmi_dsc_get_slice_height - get the dsc slice_height
 * @vactive: Vactive of a display mode
 *
 * @return: appropriate dsc slice height for a given mode.
 */
int intel_hdmi_dsc_get_slice_height(int vactive)
{
	int slice_height;

	/*
	 * Slice Height determination : HDMI2.1 Section 7.7.5.2
	 * Select smallest slice height >=96, that results in a valid PPS and
	 * requires minimum padding lines required for final slice.
	 *
	 * Assumption : Vactive is even.
	 */
	for (slice_height = 96; slice_height <= vactive; slice_height += 2)
		if (vactive % slice_height == 0)
			return slice_height;

	return 0;
}

/*
 * intel_hdmi_dsc_get_num_slices - get no. of dsc slices based on dsc encoder
 * and dsc decoder capabilities
 *
 * @crtc_state: intel crtc_state
 * @src_max_slices: maximum slices supported by the DSC encoder
 * @src_max_slice_width: maximum slice width supported by DSC encoder
 * @hdmi_max_slices: maximum slices supported by sink DSC decoder
 * @hdmi_throughput: maximum clock per slice (MHz) supported by HDMI sink
 *
 * @return: num of dsc slices that can be supported by the dsc encoder
 * and decoder.
 */
int
intel_hdmi_dsc_get_num_slices(const struct intel_crtc_state *crtc_state,
			      int src_max_slices, int src_max_slice_width,
			      int hdmi_max_slices, int hdmi_throughput)
{
/* Pixel rates in KPixels/sec */
#define HDMI_DSC_PEAK_PIXEL_RATE		2720000
/*
 * Rates at which the source and sink are required to process pixels in each
 * slice, can be two levels: either atleast 340000KHz or atleast 40000KHz.
 */
#define HDMI_DSC_MAX_ENC_THROUGHPUT_0		340000
#define HDMI_DSC_MAX_ENC_THROUGHPUT_1		400000

/* Spec limits the slice width to 2720 pixels */
#define MAX_HDMI_SLICE_WIDTH			2720
	int kslice_adjust;
	int adjusted_clk_khz;
	int min_slices;
	int target_slices;
	int max_throughput; /* max clock freq. in khz per slice */
	int max_slice_width;
	int slice_width;
	int pixel_clock = crtc_state->hw.adjusted_mode.crtc_clock;

	if (!hdmi_throughput)
		return 0;

	/*
	 * Slice Width determination : HDMI2.1 Section 7.7.5.1
	 * kslice_adjust factor for 4:2:0, and 4:2:2 formats is 0.5, where as
	 * for 4:4:4 is 1.0. Multiplying these factors by 10 and later
	 * dividing adjusted clock value by 10.
	 */
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444 ||
	    crtc_state->output_format == INTEL_OUTPUT_FORMAT_RGB)
		kslice_adjust = 10;
	else
		kslice_adjust = 5;

	/*
	 * As per spec, the rate at which the source and the sink process
	 * the pixels per slice are at two levels: atleast 340Mhz or 400Mhz.
	 * This depends upon the pixel clock rate and output formats
	 * (kslice adjust).
	 * If pixel clock * kslice adjust >= 2720MHz slices can be processed
	 * at max 340MHz, otherwise they can be processed at max 400MHz.
	 */

	adjusted_clk_khz = DIV_ROUND_UP(kslice_adjust * pixel_clock, 10);

	if (adjusted_clk_khz <= HDMI_DSC_PEAK_PIXEL_RATE)
		max_throughput = HDMI_DSC_MAX_ENC_THROUGHPUT_0;
	else
		max_throughput = HDMI_DSC_MAX_ENC_THROUGHPUT_1;

	/*
	 * Taking into account the sink's capability for maximum
	 * clock per slice (in MHz) as read from HF-VSDB.
	 */
	max_throughput = min(max_throughput, hdmi_throughput * 1000);

	min_slices = DIV_ROUND_UP(adjusted_clk_khz, max_throughput);
	max_slice_width = min(MAX_HDMI_SLICE_WIDTH, src_max_slice_width);

	/*
	 * Keep on increasing the num of slices/line, starting from min_slices
	 * per line till we get such a number, for which the slice_width is
	 * just less than max_slice_width. The slices/line selected should be
	 * less than or equal to the max horizontal slices that the combination
	 * of PCON encoder and HDMI decoder can support.
	 */
	slice_width = max_slice_width;

	do {
		if (min_slices <= 1 && src_max_slices >= 1 && hdmi_max_slices >= 1)
			target_slices = 1;
		else if (min_slices <= 2 && src_max_slices >= 2 && hdmi_max_slices >= 2)
			target_slices = 2;
		else if (min_slices <= 4 && src_max_slices >= 4 && hdmi_max_slices >= 4)
			target_slices = 4;
		else if (min_slices <= 8 && src_max_slices >= 8 && hdmi_max_slices >= 8)
			target_slices = 8;
		else if (min_slices <= 12 && src_max_slices >= 12 && hdmi_max_slices >= 12)
			target_slices = 12;
		else if (min_slices <= 16 && src_max_slices >= 16 && hdmi_max_slices >= 16)
			target_slices = 16;
		else
			return 0;

		slice_width = DIV_ROUND_UP(crtc_state->hw.adjusted_mode.hdisplay, target_slices);
		if (slice_width >= max_slice_width)
			min_slices = target_slices + 1;
	} while (slice_width >= max_slice_width);

	return target_slices;
}

/*
 * intel_hdmi_dsc_get_bpp - get the appropriate compressed bits_per_pixel based on
 * source and sink capabilities.
 *
 * @src_fraction_bpp: fractional bpp supported by the source
 * @slice_width: dsc slice width supported by the source and sink
 * @num_slices: num of slices supported by the source and sink
 * @output_format: video output format
 * @hdmi_all_bpp: sink supports decoding of 1/16th bpp setting
 * @hdmi_max_chunk_bytes: max bytes in a line of chunks supported by sink
 *
 * @return: compressed bits_per_pixel in step of 1/16 of bits_per_pixel
 */
int
intel_hdmi_dsc_get_bpp(int src_fractional_bpp, int slice_width, int num_slices,
		       int output_format, bool hdmi_all_bpp,
		       int hdmi_max_chunk_bytes)
{
	int max_dsc_bpp, min_dsc_bpp;
	int target_bytes;
	bool bpp_found = false;
	int bpp_decrement_x16;
	int bpp_target;
	int bpp_target_x16;

	/*
	 * Get min bpp and max bpp as per Table 7.23, in HDMI2.1 spec
	 * Start with the max bpp and keep on decrementing with
	 * fractional bpp, if supported by PCON DSC encoder
	 *
	 * for each bpp we check if no of bytes can be supported by HDMI sink
	 */

	/* Assuming: bpc as 8*/
	if (output_format == INTEL_OUTPUT_FORMAT_YCBCR420) {
		min_dsc_bpp = 6;
		max_dsc_bpp = 3 * 4; /* 3*bpc/2 */
	} else if (output_format == INTEL_OUTPUT_FORMAT_YCBCR444 ||
		   output_format == INTEL_OUTPUT_FORMAT_RGB) {
		min_dsc_bpp = 8;
		max_dsc_bpp = 3 * 8; /* 3*bpc */
	} else {
		/* Assuming 4:2:2 encoding */
		min_dsc_bpp = 7;
		max_dsc_bpp = 2 * 8; /* 2*bpc */
	}

	/*
	 * Taking into account if all dsc_all_bpp supported by HDMI2.1 sink
	 * Section 7.7.34 : Source shall not enable compressed Video
	 * Transport with bpp_target settings above 12 bpp unless
	 * DSC_all_bpp is set to 1.
	 */
	if (!hdmi_all_bpp)
		max_dsc_bpp = min(max_dsc_bpp, 12);

	/*
	 * The Sink has a limit of compressed data in bytes for a scanline,
	 * as described in max_chunk_bytes field in HFVSDB block of edid.
	 * The no. of bytes depend on the target bits per pixel that the
	 * source configures. So we start with the max_bpp and calculate
	 * the target_chunk_bytes. We keep on decrementing the target_bpp,
	 * till we get the target_chunk_bytes just less than what the sink's
	 * max_chunk_bytes, or else till we reach the min_dsc_bpp.
	 *
	 * The decrement is according to the fractional support from PCON DSC
	 * encoder. For fractional BPP we use bpp_target as a multiple of 16.
	 *
	 * bpp_target_x16 = bpp_target * 16
	 * So we need to decrement by {1, 2, 4, 8, 16} for fractional bpps
	 * {1/16, 1/8, 1/4, 1/2, 1} respectively.
	 */

	bpp_target = max_dsc_bpp;

	/* src does not support fractional bpp implies decrement by 16 for bppx16 */
	if (!src_fractional_bpp)
		src_fractional_bpp = 1;
	bpp_decrement_x16 = DIV_ROUND_UP(16, src_fractional_bpp);
	bpp_target_x16 = (bpp_target * 16) - bpp_decrement_x16;

	while (bpp_target_x16 > (min_dsc_bpp * 16)) {
		int bpp;

		bpp = DIV_ROUND_UP(bpp_target_x16, 16);
		target_bytes = DIV_ROUND_UP((num_slices * slice_width * bpp), 8);
		if (target_bytes <= hdmi_max_chunk_bytes) {
			bpp_found = true;
			break;
		}
		bpp_target_x16 -= bpp_decrement_x16;
	}
	if (bpp_found)
		return bpp_target_x16;

	return 0;
}

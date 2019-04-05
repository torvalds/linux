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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_hdcp.h>
#include <drm/drm_scdc_helper.h>
#include <drm/i915_drm.h>
#include <drm/intel_lpe_audio.h>

#include "i915_drv.h"
#include "intel_audio.h"
#include "intel_connector.h"
#include "intel_ddi.h"
#include "intel_drv.h"
#include "intel_hdcp.h"
#include "intel_lspcon.h"
#include "intel_sdvo.h"
#include "intel_panel.h"

static struct drm_device *intel_hdmi_to_dev(struct intel_hdmi *intel_hdmi)
{
	return hdmi_to_dig_port(intel_hdmi)->base.base.dev;
}

static void
assert_hdmi_port_disabled(struct intel_hdmi *intel_hdmi)
{
	struct drm_device *dev = intel_hdmi_to_dev(intel_hdmi);
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 enabled_bits;

	enabled_bits = HAS_DDI(dev_priv) ? DDI_BUF_CTL_ENABLE : SDVO_ENABLE;

	WARN(I915_READ(intel_hdmi->hdmi_reg) & enabled_bits,
	     "HDMI port enabled, expecting disabled\n");
}

static void
assert_hdmi_transcoder_func_disabled(struct drm_i915_private *dev_priv,
				     enum transcoder cpu_transcoder)
{
	WARN(I915_READ(TRANS_DDI_FUNC_CTL(cpu_transcoder)) &
	     TRANS_DDI_FUNC_ENABLE,
	     "HDMI transcoder function enabled, expecting disabled\n");
}

struct intel_hdmi *enc_to_intel_hdmi(struct drm_encoder *encoder)
{
	struct intel_digital_port *intel_dig_port =
		container_of(encoder, struct intel_digital_port, base.base);
	return &intel_dig_port->hdmi;
}

static struct intel_hdmi *intel_attached_hdmi(struct drm_connector *connector)
{
	return enc_to_intel_hdmi(&intel_attached_encoder(connector)->base);
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
	default:
		MISSING_CASE(type);
		return INVALID_MMIO_REG;
	}
}

static int hsw_dip_data_size(unsigned int type)
{
	switch (type) {
	case DP_SDP_VSC:
		return VIDEO_DIP_VSC_DATA_SIZE;
	case DP_SDP_PPS:
		return VIDEO_DIP_PPS_DATA_SIZE;
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
	u32 val = I915_READ(VIDEO_DIP_CTL);
	int i;

	WARN(!(val & VIDEO_DIP_ENABLE), "Writing DIP with CTL reg disabled\n");

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	val &= ~g4x_infoframe_enable(type);

	I915_WRITE(VIDEO_DIP_CTL, val);

	mmiowb();
	for (i = 0; i < len; i += 4) {
		I915_WRITE(VIDEO_DIP_DATA, *data);
		data++;
	}
	/* Write every possible data byte to force correct ECC calculation. */
	for (; i < VIDEO_DIP_DATA_SIZE; i += 4)
		I915_WRITE(VIDEO_DIP_DATA, 0);
	mmiowb();

	val |= g4x_infoframe_enable(type);
	val &= ~VIDEO_DIP_FREQ_MASK;
	val |= VIDEO_DIP_FREQ_VSYNC;

	I915_WRITE(VIDEO_DIP_CTL, val);
	POSTING_READ(VIDEO_DIP_CTL);
}

static void g4x_read_infoframe(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type,
			       void *frame, ssize_t len)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 val, *data = frame;
	int i;

	val = I915_READ(VIDEO_DIP_CTL);

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	I915_WRITE(VIDEO_DIP_CTL, val);

	for (i = 0; i < len; i += 4)
		*data++ = I915_READ(VIDEO_DIP_DATA);
}

static u32 g4x_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 val = I915_READ(VIDEO_DIP_CTL);

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
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	i915_reg_t reg = TVIDEO_DIP_CTL(intel_crtc->pipe);
	u32 val = I915_READ(reg);
	int i;

	WARN(!(val & VIDEO_DIP_ENABLE), "Writing DIP with CTL reg disabled\n");

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	val &= ~g4x_infoframe_enable(type);

	I915_WRITE(reg, val);

	mmiowb();
	for (i = 0; i < len; i += 4) {
		I915_WRITE(TVIDEO_DIP_DATA(intel_crtc->pipe), *data);
		data++;
	}
	/* Write every possible data byte to force correct ECC calculation. */
	for (; i < VIDEO_DIP_DATA_SIZE; i += 4)
		I915_WRITE(TVIDEO_DIP_DATA(intel_crtc->pipe), 0);
	mmiowb();

	val |= g4x_infoframe_enable(type);
	val &= ~VIDEO_DIP_FREQ_MASK;
	val |= VIDEO_DIP_FREQ_VSYNC;

	I915_WRITE(reg, val);
	POSTING_READ(reg);
}

static void ibx_read_infoframe(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type,
			       void *frame, ssize_t len)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	u32 val, *data = frame;
	int i;

	val = I915_READ(TVIDEO_DIP_CTL(crtc->pipe));

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	I915_WRITE(TVIDEO_DIP_CTL(crtc->pipe), val);

	for (i = 0; i < len; i += 4)
		*data++ = I915_READ(TVIDEO_DIP_DATA(crtc->pipe));
}

static u32 ibx_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum pipe pipe = to_intel_crtc(pipe_config->base.crtc)->pipe;
	i915_reg_t reg = TVIDEO_DIP_CTL(pipe);
	u32 val = I915_READ(reg);

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
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	i915_reg_t reg = TVIDEO_DIP_CTL(intel_crtc->pipe);
	u32 val = I915_READ(reg);
	int i;

	WARN(!(val & VIDEO_DIP_ENABLE), "Writing DIP with CTL reg disabled\n");

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	/* The DIP control register spec says that we need to update the AVI
	 * infoframe without clearing its enable bit */
	if (type != HDMI_INFOFRAME_TYPE_AVI)
		val &= ~g4x_infoframe_enable(type);

	I915_WRITE(reg, val);

	mmiowb();
	for (i = 0; i < len; i += 4) {
		I915_WRITE(TVIDEO_DIP_DATA(intel_crtc->pipe), *data);
		data++;
	}
	/* Write every possible data byte to force correct ECC calculation. */
	for (; i < VIDEO_DIP_DATA_SIZE; i += 4)
		I915_WRITE(TVIDEO_DIP_DATA(intel_crtc->pipe), 0);
	mmiowb();

	val |= g4x_infoframe_enable(type);
	val &= ~VIDEO_DIP_FREQ_MASK;
	val |= VIDEO_DIP_FREQ_VSYNC;

	I915_WRITE(reg, val);
	POSTING_READ(reg);
}

static void cpt_read_infoframe(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type,
			       void *frame, ssize_t len)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	u32 val, *data = frame;
	int i;

	val = I915_READ(TVIDEO_DIP_CTL(crtc->pipe));

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	I915_WRITE(TVIDEO_DIP_CTL(crtc->pipe), val);

	for (i = 0; i < len; i += 4)
		*data++ = I915_READ(TVIDEO_DIP_DATA(crtc->pipe));
}

static u32 cpt_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum pipe pipe = to_intel_crtc(pipe_config->base.crtc)->pipe;
	u32 val = I915_READ(TVIDEO_DIP_CTL(pipe));

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
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	i915_reg_t reg = VLV_TVIDEO_DIP_CTL(intel_crtc->pipe);
	u32 val = I915_READ(reg);
	int i;

	WARN(!(val & VIDEO_DIP_ENABLE), "Writing DIP with CTL reg disabled\n");

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	val &= ~g4x_infoframe_enable(type);

	I915_WRITE(reg, val);

	mmiowb();
	for (i = 0; i < len; i += 4) {
		I915_WRITE(VLV_TVIDEO_DIP_DATA(intel_crtc->pipe), *data);
		data++;
	}
	/* Write every possible data byte to force correct ECC calculation. */
	for (; i < VIDEO_DIP_DATA_SIZE; i += 4)
		I915_WRITE(VLV_TVIDEO_DIP_DATA(intel_crtc->pipe), 0);
	mmiowb();

	val |= g4x_infoframe_enable(type);
	val &= ~VIDEO_DIP_FREQ_MASK;
	val |= VIDEO_DIP_FREQ_VSYNC;

	I915_WRITE(reg, val);
	POSTING_READ(reg);
}

static void vlv_read_infoframe(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type,
			       void *frame, ssize_t len)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	u32 val, *data = frame;
	int i;

	val = I915_READ(VLV_TVIDEO_DIP_CTL(crtc->pipe));

	val &= ~(VIDEO_DIP_SELECT_MASK | 0xf); /* clear DIP data offset */
	val |= g4x_infoframe_index(type);

	I915_WRITE(VLV_TVIDEO_DIP_CTL(crtc->pipe), val);

	for (i = 0; i < len; i += 4)
		*data++ = I915_READ(VLV_TVIDEO_DIP_DATA(crtc->pipe));
}

static u32 vlv_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum pipe pipe = to_intel_crtc(pipe_config->base.crtc)->pipe;
	u32 val = I915_READ(VLV_TVIDEO_DIP_CTL(pipe));

	if ((val & VIDEO_DIP_ENABLE) == 0)
		return 0;

	if ((val & VIDEO_DIP_PORT_MASK) != VIDEO_DIP_PORT(encoder->port))
		return 0;

	return val & (VIDEO_DIP_ENABLE_AVI |
		      VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
		      VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);
}

static void hsw_write_infoframe(struct intel_encoder *encoder,
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
	u32 val = I915_READ(ctl_reg);

	data_size = hsw_dip_data_size(type);

	val &= ~hsw_infoframe_enable(type);
	I915_WRITE(ctl_reg, val);

	mmiowb();
	for (i = 0; i < len; i += 4) {
		I915_WRITE(hsw_dip_data_reg(dev_priv, cpu_transcoder,
					    type, i >> 2), *data);
		data++;
	}
	/* Write every possible data byte to force correct ECC calculation. */
	for (; i < data_size; i += 4)
		I915_WRITE(hsw_dip_data_reg(dev_priv, cpu_transcoder,
					    type, i >> 2), 0);
	mmiowb();

	val |= hsw_infoframe_enable(type);
	I915_WRITE(ctl_reg, val);
	POSTING_READ(ctl_reg);
}

static void hsw_read_infoframe(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type,
			       void *frame, ssize_t len)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 val, *data = frame;
	int i;

	val = I915_READ(HSW_TVIDEO_DIP_CTL(cpu_transcoder));

	for (i = 0; i < len; i += 4)
		*data++ = I915_READ(hsw_dip_data_reg(dev_priv, cpu_transcoder,
						     type, i >> 2));
}

static u32 hsw_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 val = I915_READ(HSW_TVIDEO_DIP_CTL(pipe_config->cpu_transcoder));

	return val & (VIDEO_DIP_ENABLE_VSC_HSW | VIDEO_DIP_ENABLE_AVI_HSW |
		      VIDEO_DIP_ENABLE_GCP_HSW | VIDEO_DIP_ENABLE_VS_HSW |
		      VIDEO_DIP_ENABLE_GMP_HSW | VIDEO_DIP_ENABLE_SPD_HSW);
}

static const u8 infoframe_type_to_idx[] = {
	HDMI_PACKET_TYPE_GENERAL_CONTROL,
	HDMI_PACKET_TYPE_GAMUT_METADATA,
	DP_SDP_VSC,
	HDMI_INFOFRAME_TYPE_AVI,
	HDMI_INFOFRAME_TYPE_SPD,
	HDMI_INFOFRAME_TYPE_VENDOR,
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
	struct intel_digital_port *dig_port = enc_to_dig_port(&encoder->base);
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
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(&encoder->base);
	u8 buffer[VIDEO_DIP_DATA_SIZE];
	ssize_t len;

	if ((crtc_state->infoframes.enable &
	     intel_hdmi_infoframe_enable(type)) == 0)
		return;

	if (WARN_ON(frame->any.type != type))
		return;

	/* see comment above for the reason for this offset */
	len = hdmi_infoframe_pack_only(frame, buffer + 1, sizeof(buffer) - 1);
	if (WARN_ON(len < 0))
		return;

	/* Insert the 'hole' (see big comment above) at position 3 */
	memmove(&buffer[0], &buffer[1], 3);
	buffer[3] = 0;
	len++;

	intel_dig_port->write_infoframe(encoder, crtc_state, type, buffer, len);
}

void intel_read_infoframe(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state,
			  enum hdmi_infoframe_type type,
			  union hdmi_infoframe *frame)
{
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(&encoder->base);
	u8 buffer[VIDEO_DIP_DATA_SIZE];
	int ret;

	if ((crtc_state->infoframes.enable &
	     intel_hdmi_infoframe_enable(type)) == 0)
		return;

	intel_dig_port->read_infoframe(encoder, crtc_state,
				       type, buffer, sizeof(buffer));

	/* Fill the 'hole' (see big comment above) at position 3 */
	memmove(&buffer[1], &buffer[0], 3);

	/* see comment above for the reason for this offset */
	ret = hdmi_infoframe_unpack(frame, buffer + 1, sizeof(buffer) - 1);
	if (ret) {
		DRM_DEBUG_KMS("Failed to unpack infoframe type 0x%02x\n", type);
		return;
	}

	if (frame->any.type != type)
		DRM_DEBUG_KMS("Found the wrong infoframe type 0x%x (expected 0x%02x)\n",
			      frame->any.type, type);
}

static bool
intel_hdmi_compute_avi_infoframe(struct intel_encoder *encoder,
				 struct intel_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct hdmi_avi_infoframe *frame = &crtc_state->infoframes.avi.avi;
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;
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

	drm_hdmi_avi_infoframe_colorspace(frame, conn_state);

	drm_hdmi_avi_infoframe_quant_range(frame, connector,
					   adjusted_mode,
					   crtc_state->limited_color_range ?
					   HDMI_QUANTIZATION_RANGE_LIMITED :
					   HDMI_QUANTIZATION_RANGE_FULL);

	drm_hdmi_avi_infoframe_content_type(frame, conn_state);

	/* TODO: handle pixel repetition for YCBCR420 outputs */

	ret = hdmi_avi_infoframe_check(frame);
	if (WARN_ON(ret))
		return false;

	return true;
}

static bool
intel_hdmi_compute_spd_infoframe(struct intel_encoder *encoder,
				 struct intel_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct hdmi_spd_infoframe *frame = &crtc_state->infoframes.spd.spd;
	int ret;

	if (!crtc_state->has_infoframe)
		return true;

	crtc_state->infoframes.enable |=
		intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_SPD);

	ret = hdmi_spd_infoframe_init(frame, "Intel", "Integrated gfx");
	if (WARN_ON(ret))
		return false;

	frame->sdi = HDMI_SPD_SDI_PC;

	ret = hdmi_spd_infoframe_check(frame);
	if (WARN_ON(ret))
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
							  &crtc_state->base.adjusted_mode);
	if (WARN_ON(ret))
		return false;

	ret = hdmi_vendor_infoframe_check(frame);
	if (WARN_ON(ret))
		return false;

	return true;
}

static void g4x_set_infoframes(struct intel_encoder *encoder,
			       bool enable,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(&encoder->base);
	struct intel_hdmi *intel_hdmi = &intel_dig_port->hdmi;
	i915_reg_t reg = VIDEO_DIP_CTL;
	u32 val = I915_READ(reg);
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
			DRM_DEBUG_KMS("video DIP still enabled on port %c\n",
				      (val & VIDEO_DIP_PORT_MASK) >> 29);
			return;
		}
		val &= ~(VIDEO_DIP_ENABLE | VIDEO_DIP_ENABLE_AVI |
			 VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_SPD);
		I915_WRITE(reg, val);
		POSTING_READ(reg);
		return;
	}

	if (port != (val & VIDEO_DIP_PORT_MASK)) {
		if (val & VIDEO_DIP_ENABLE) {
			DRM_DEBUG_KMS("video DIP already enabled on port %c\n",
				      (val & VIDEO_DIP_PORT_MASK) >> 29);
			return;
		}
		val &= ~VIDEO_DIP_PORT_MASK;
		val |= port;
	}

	val |= VIDEO_DIP_ENABLE;
	val &= ~(VIDEO_DIP_ENABLE_AVI |
		 VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_SPD);

	I915_WRITE(reg, val);
	POSTING_READ(reg);

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

static bool hdmi_sink_is_deep_color(const struct drm_connector_state *conn_state)
{
	struct drm_connector *connector = conn_state->connector;

	/*
	 * HDMI cloning is only supported on g4x which doesn't
	 * support deep color or GCP infoframes anyway so no
	 * need to worry about multiple HDMI sinks here.
	 */

	return connector->display_info.bpc > 8;
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
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
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

	I915_WRITE(reg, crtc_state->infoframes.gcp);

	return true;
}

void intel_hdmi_read_gcp_infoframe(struct intel_encoder *encoder,
				   struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
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

	crtc_state->infoframes.gcp = I915_READ(reg);
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

	/* Indicate color depth whenever the sink supports deep color */
	if (hdmi_sink_is_deep_color(conn_state))
		crtc_state->infoframes.gcp |= GCP_COLOR_INDICATION;

	/* Enable default_phase whenever the display mode is suitably aligned */
	if (gcp_default_phase_possible(crtc_state->pipe_bpp,
				       &crtc_state->base.adjusted_mode))
		crtc_state->infoframes.gcp |= GCP_DEFAULT_PHASE_ENABLE;
}

static void ibx_set_infoframes(struct intel_encoder *encoder,
			       bool enable,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(&encoder->base);
	struct intel_hdmi *intel_hdmi = &intel_dig_port->hdmi;
	i915_reg_t reg = TVIDEO_DIP_CTL(intel_crtc->pipe);
	u32 val = I915_READ(reg);
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
		I915_WRITE(reg, val);
		POSTING_READ(reg);
		return;
	}

	if (port != (val & VIDEO_DIP_PORT_MASK)) {
		WARN(val & VIDEO_DIP_ENABLE,
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

	I915_WRITE(reg, val);
	POSTING_READ(reg);

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
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	i915_reg_t reg = TVIDEO_DIP_CTL(intel_crtc->pipe);
	u32 val = I915_READ(reg);

	assert_hdmi_port_disabled(intel_hdmi);

	/* See the big comment in g4x_set_infoframes() */
	val |= VIDEO_DIP_SELECT_AVI | VIDEO_DIP_FREQ_VSYNC;

	if (!enable) {
		if (!(val & VIDEO_DIP_ENABLE))
			return;
		val &= ~(VIDEO_DIP_ENABLE | VIDEO_DIP_ENABLE_AVI |
			 VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
			 VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);
		I915_WRITE(reg, val);
		POSTING_READ(reg);
		return;
	}

	/* Set both together, unset both together: see the spec. */
	val |= VIDEO_DIP_ENABLE | VIDEO_DIP_ENABLE_AVI;
	val &= ~(VIDEO_DIP_ENABLE_VENDOR | VIDEO_DIP_ENABLE_GAMUT |
		 VIDEO_DIP_ENABLE_SPD | VIDEO_DIP_ENABLE_GCP);

	if (intel_hdmi_set_gcp_infoframe(encoder, crtc_state, conn_state))
		val |= VIDEO_DIP_ENABLE_GCP;

	I915_WRITE(reg, val);
	POSTING_READ(reg);

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
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	i915_reg_t reg = VLV_TVIDEO_DIP_CTL(intel_crtc->pipe);
	u32 val = I915_READ(reg);
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
		I915_WRITE(reg, val);
		POSTING_READ(reg);
		return;
	}

	if (port != (val & VIDEO_DIP_PORT_MASK)) {
		WARN(val & VIDEO_DIP_ENABLE,
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

	I915_WRITE(reg, val);
	POSTING_READ(reg);

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
	u32 val = I915_READ(reg);

	assert_hdmi_transcoder_func_disabled(dev_priv,
					     crtc_state->cpu_transcoder);

	val &= ~(VIDEO_DIP_ENABLE_VSC_HSW | VIDEO_DIP_ENABLE_AVI_HSW |
		 VIDEO_DIP_ENABLE_GCP_HSW | VIDEO_DIP_ENABLE_VS_HSW |
		 VIDEO_DIP_ENABLE_GMP_HSW | VIDEO_DIP_ENABLE_SPD_HSW);

	if (!enable) {
		I915_WRITE(reg, val);
		POSTING_READ(reg);
		return;
	}

	if (intel_hdmi_set_gcp_infoframe(encoder, crtc_state, conn_state))
		val |= VIDEO_DIP_ENABLE_GCP_HSW;

	I915_WRITE(reg, val);
	POSTING_READ(reg);

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

void intel_dp_dual_mode_set_tmds_output(struct intel_hdmi *hdmi, bool enable)
{
	struct drm_i915_private *dev_priv = to_i915(intel_hdmi_to_dev(hdmi));
	struct i2c_adapter *adapter =
		intel_gmbus_get_adapter(dev_priv, hdmi->ddc_bus);

	if (hdmi->dp_dual_mode.type < DRM_DP_DUAL_MODE_TYPE2_DVI)
		return;

	DRM_DEBUG_KMS("%s DP dual mode adaptor TMDS output\n",
		      enable ? "Enabling" : "Disabling");

	drm_dp_dual_mode_set_tmds_output(hdmi->dp_dual_mode.type,
					 adapter, enable);
}

static int intel_hdmi_hdcp_read(struct intel_digital_port *intel_dig_port,
				unsigned int offset, void *buffer, size_t size)
{
	struct intel_hdmi *hdmi = &intel_dig_port->hdmi;
	struct drm_i915_private *dev_priv =
		intel_dig_port->base.base.dev->dev_private;
	struct i2c_adapter *adapter = intel_gmbus_get_adapter(dev_priv,
							      hdmi->ddc_bus);
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
	ret = i2c_transfer(adapter, msgs, ARRAY_SIZE(msgs));
	if (ret == ARRAY_SIZE(msgs))
		return 0;
	return ret >= 0 ? -EIO : ret;
}

static int intel_hdmi_hdcp_write(struct intel_digital_port *intel_dig_port,
				 unsigned int offset, void *buffer, size_t size)
{
	struct intel_hdmi *hdmi = &intel_dig_port->hdmi;
	struct drm_i915_private *dev_priv =
		intel_dig_port->base.base.dev->dev_private;
	struct i2c_adapter *adapter = intel_gmbus_get_adapter(dev_priv,
							      hdmi->ddc_bus);
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

	ret = i2c_transfer(adapter, &msg, 1);
	if (ret == 1)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

	kfree(write_buf);
	return ret;
}

static
int intel_hdmi_hdcp_write_an_aksv(struct intel_digital_port *intel_dig_port,
				  u8 *an)
{
	struct intel_hdmi *hdmi = &intel_dig_port->hdmi;
	struct drm_i915_private *dev_priv =
		intel_dig_port->base.base.dev->dev_private;
	struct i2c_adapter *adapter = intel_gmbus_get_adapter(dev_priv,
							      hdmi->ddc_bus);
	int ret;

	ret = intel_hdmi_hdcp_write(intel_dig_port, DRM_HDCP_DDC_AN, an,
				    DRM_HDCP_AN_LEN);
	if (ret) {
		DRM_DEBUG_KMS("Write An over DDC failed (%d)\n", ret);
		return ret;
	}

	ret = intel_gmbus_output_aksv(adapter);
	if (ret < 0) {
		DRM_DEBUG_KMS("Failed to output aksv (%d)\n", ret);
		return ret;
	}
	return 0;
}

static int intel_hdmi_hdcp_read_bksv(struct intel_digital_port *intel_dig_port,
				     u8 *bksv)
{
	int ret;
	ret = intel_hdmi_hdcp_read(intel_dig_port, DRM_HDCP_DDC_BKSV, bksv,
				   DRM_HDCP_KSV_LEN);
	if (ret)
		DRM_DEBUG_KMS("Read Bksv over DDC failed (%d)\n", ret);
	return ret;
}

static
int intel_hdmi_hdcp_read_bstatus(struct intel_digital_port *intel_dig_port,
				 u8 *bstatus)
{
	int ret;
	ret = intel_hdmi_hdcp_read(intel_dig_port, DRM_HDCP_DDC_BSTATUS,
				   bstatus, DRM_HDCP_BSTATUS_LEN);
	if (ret)
		DRM_DEBUG_KMS("Read bstatus over DDC failed (%d)\n", ret);
	return ret;
}

static
int intel_hdmi_hdcp_repeater_present(struct intel_digital_port *intel_dig_port,
				     bool *repeater_present)
{
	int ret;
	u8 val;

	ret = intel_hdmi_hdcp_read(intel_dig_port, DRM_HDCP_DDC_BCAPS, &val, 1);
	if (ret) {
		DRM_DEBUG_KMS("Read bcaps over DDC failed (%d)\n", ret);
		return ret;
	}
	*repeater_present = val & DRM_HDCP_DDC_BCAPS_REPEATER_PRESENT;
	return 0;
}

static
int intel_hdmi_hdcp_read_ri_prime(struct intel_digital_port *intel_dig_port,
				  u8 *ri_prime)
{
	int ret;
	ret = intel_hdmi_hdcp_read(intel_dig_port, DRM_HDCP_DDC_RI_PRIME,
				   ri_prime, DRM_HDCP_RI_LEN);
	if (ret)
		DRM_DEBUG_KMS("Read Ri' over DDC failed (%d)\n", ret);
	return ret;
}

static
int intel_hdmi_hdcp_read_ksv_ready(struct intel_digital_port *intel_dig_port,
				   bool *ksv_ready)
{
	int ret;
	u8 val;

	ret = intel_hdmi_hdcp_read(intel_dig_port, DRM_HDCP_DDC_BCAPS, &val, 1);
	if (ret) {
		DRM_DEBUG_KMS("Read bcaps over DDC failed (%d)\n", ret);
		return ret;
	}
	*ksv_ready = val & DRM_HDCP_DDC_BCAPS_KSV_FIFO_READY;
	return 0;
}

static
int intel_hdmi_hdcp_read_ksv_fifo(struct intel_digital_port *intel_dig_port,
				  int num_downstream, u8 *ksv_fifo)
{
	int ret;
	ret = intel_hdmi_hdcp_read(intel_dig_port, DRM_HDCP_DDC_KSV_FIFO,
				   ksv_fifo, num_downstream * DRM_HDCP_KSV_LEN);
	if (ret) {
		DRM_DEBUG_KMS("Read ksv fifo over DDC failed (%d)\n", ret);
		return ret;
	}
	return 0;
}

static
int intel_hdmi_hdcp_read_v_prime_part(struct intel_digital_port *intel_dig_port,
				      int i, u32 *part)
{
	int ret;

	if (i >= DRM_HDCP_V_PRIME_NUM_PARTS)
		return -EINVAL;

	ret = intel_hdmi_hdcp_read(intel_dig_port, DRM_HDCP_DDC_V_PRIME(i),
				   part, DRM_HDCP_V_PRIME_PART_LEN);
	if (ret)
		DRM_DEBUG_KMS("Read V'[%d] over DDC failed (%d)\n", i, ret);
	return ret;
}

static int kbl_repositioning_enc_en_signal(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct drm_crtc *crtc = connector->base.state->crtc;
	struct intel_crtc *intel_crtc = container_of(crtc,
						     struct intel_crtc, base);
	u32 scanline;
	int ret;

	for (;;) {
		scanline = I915_READ(PIPEDSL(intel_crtc->pipe));
		if (scanline > 100 && scanline < 200)
			break;
		usleep_range(25, 50);
	}

	ret = intel_ddi_toggle_hdcp_signalling(&intel_dig_port->base, false);
	if (ret) {
		DRM_ERROR("Disable HDCP signalling failed (%d)\n", ret);
		return ret;
	}
	ret = intel_ddi_toggle_hdcp_signalling(&intel_dig_port->base, true);
	if (ret) {
		DRM_ERROR("Enable HDCP signalling failed (%d)\n", ret);
		return ret;
	}

	return 0;
}

static
int intel_hdmi_hdcp_toggle_signalling(struct intel_digital_port *intel_dig_port,
				      bool enable)
{
	struct intel_hdmi *hdmi = &intel_dig_port->hdmi;
	struct intel_connector *connector = hdmi->attached_connector;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	int ret;

	if (!enable)
		usleep_range(6, 60); /* Bspec says >= 6us */

	ret = intel_ddi_toggle_hdcp_signalling(&intel_dig_port->base, enable);
	if (ret) {
		DRM_ERROR("%s HDCP signalling failed (%d)\n",
			  enable ? "Enable" : "Disable", ret);
		return ret;
	}

	/*
	 * WA: To fix incorrect positioning of the window of
	 * opportunity and enc_en signalling in KABYLAKE.
	 */
	if (IS_KABYLAKE(dev_priv) && enable)
		return kbl_repositioning_enc_en_signal(connector);

	return 0;
}

static
bool intel_hdmi_hdcp_check_link(struct intel_digital_port *intel_dig_port)
{
	struct drm_i915_private *dev_priv =
		intel_dig_port->base.base.dev->dev_private;
	enum port port = intel_dig_port->base.port;
	int ret;
	union {
		u32 reg;
		u8 shim[DRM_HDCP_RI_LEN];
	} ri;

	ret = intel_hdmi_hdcp_read_ri_prime(intel_dig_port, ri.shim);
	if (ret)
		return false;

	I915_WRITE(PORT_HDCP_RPRIME(port), ri.reg);

	/* Wait for Ri prime match */
	if (wait_for(I915_READ(PORT_HDCP_STATUS(port)) &
		     (HDCP_STATUS_RI_MATCH | HDCP_STATUS_ENC), 1)) {
		DRM_ERROR("Ri' mismatch detected, link check failed (%x)\n",
			  I915_READ(PORT_HDCP_STATUS(port)));
		return false;
	}
	return true;
}

static struct hdcp2_hdmi_msg_data {
	u8 msg_id;
	u32 timeout;
	u32 timeout2;
	} hdcp2_msg_data[] = {
		{HDCP_2_2_AKE_INIT, 0, 0},
		{HDCP_2_2_AKE_SEND_CERT, HDCP_2_2_CERT_TIMEOUT_MS, 0},
		{HDCP_2_2_AKE_NO_STORED_KM, 0, 0},
		{HDCP_2_2_AKE_STORED_KM, 0, 0},
		{HDCP_2_2_AKE_SEND_HPRIME, HDCP_2_2_HPRIME_PAIRED_TIMEOUT_MS,
				HDCP_2_2_HPRIME_NO_PAIRED_TIMEOUT_MS},
		{HDCP_2_2_AKE_SEND_PAIRING_INFO, HDCP_2_2_PAIRING_TIMEOUT_MS,
				0},
		{HDCP_2_2_LC_INIT, 0, 0},
		{HDCP_2_2_LC_SEND_LPRIME, HDCP_2_2_HDMI_LPRIME_TIMEOUT_MS, 0},
		{HDCP_2_2_SKE_SEND_EKS, 0, 0},
		{HDCP_2_2_REP_SEND_RECVID_LIST,
				HDCP_2_2_RECVID_LIST_TIMEOUT_MS, 0},
		{HDCP_2_2_REP_SEND_ACK, 0, 0},
		{HDCP_2_2_REP_STREAM_MANAGE, 0, 0},
		{HDCP_2_2_REP_STREAM_READY, HDCP_2_2_STREAM_READY_TIMEOUT_MS,
				0},
	};

static
int intel_hdmi_hdcp2_read_rx_status(struct intel_digital_port *intel_dig_port,
				    u8 *rx_status)
{
	return intel_hdmi_hdcp_read(intel_dig_port,
				    HDCP_2_2_HDMI_REG_RXSTATUS_OFFSET,
				    rx_status,
				    HDCP_2_2_HDMI_RXSTATUS_LEN);
}

static int get_hdcp2_msg_timeout(u8 msg_id, bool is_paired)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hdcp2_msg_data); i++)
		if (hdcp2_msg_data[i].msg_id == msg_id &&
		    (msg_id != HDCP_2_2_AKE_SEND_HPRIME || is_paired))
			return hdcp2_msg_data[i].timeout;
		else if (hdcp2_msg_data[i].msg_id == msg_id)
			return hdcp2_msg_data[i].timeout2;

	return -EINVAL;
}

static inline
int hdcp2_detect_msg_availability(struct intel_digital_port *intel_digital_port,
				  u8 msg_id, bool *msg_ready,
				  ssize_t *msg_sz)
{
	u8 rx_status[HDCP_2_2_HDMI_RXSTATUS_LEN];
	int ret;

	ret = intel_hdmi_hdcp2_read_rx_status(intel_digital_port, rx_status);
	if (ret < 0) {
		DRM_DEBUG_KMS("rx_status read failed. Err %d\n", ret);
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
intel_hdmi_hdcp2_wait_for_msg(struct intel_digital_port *intel_dig_port,
			      u8 msg_id, bool paired)
{
	bool msg_ready = false;
	int timeout, ret;
	ssize_t msg_sz = 0;

	timeout = get_hdcp2_msg_timeout(msg_id, paired);
	if (timeout < 0)
		return timeout;

	ret = __wait_for(ret = hdcp2_detect_msg_availability(intel_dig_port,
							     msg_id, &msg_ready,
							     &msg_sz),
			 !ret && msg_ready && msg_sz, timeout * 1000,
			 1000, 5 * 1000);
	if (ret)
		DRM_DEBUG_KMS("msg_id: %d, ret: %d, timeout: %d\n",
			      msg_id, ret, timeout);

	return ret ? ret : msg_sz;
}

static
int intel_hdmi_hdcp2_write_msg(struct intel_digital_port *intel_dig_port,
			       void *buf, size_t size)
{
	unsigned int offset;

	offset = HDCP_2_2_HDMI_REG_WR_MSG_OFFSET;
	return intel_hdmi_hdcp_write(intel_dig_port, offset, buf, size);
}

static
int intel_hdmi_hdcp2_read_msg(struct intel_digital_port *intel_dig_port,
			      u8 msg_id, void *buf, size_t size)
{
	struct intel_hdmi *hdmi = &intel_dig_port->hdmi;
	struct intel_hdcp *hdcp = &hdmi->attached_connector->hdcp;
	unsigned int offset;
	ssize_t ret;

	ret = intel_hdmi_hdcp2_wait_for_msg(intel_dig_port, msg_id,
					    hdcp->is_paired);
	if (ret < 0)
		return ret;

	/*
	 * Available msg size should be equal to or lesser than the
	 * available buffer.
	 */
	if (ret > size) {
		DRM_DEBUG_KMS("msg_sz(%zd) is more than exp size(%zu)\n",
			      ret, size);
		return -1;
	}

	offset = HDCP_2_2_HDMI_REG_RD_MSG_OFFSET;
	ret = intel_hdmi_hdcp_read(intel_dig_port, offset, buf, ret);
	if (ret)
		DRM_DEBUG_KMS("Failed to read msg_id: %d(%zd)\n", msg_id, ret);

	return ret;
}

static
int intel_hdmi_hdcp2_check_link(struct intel_digital_port *intel_dig_port)
{
	u8 rx_status[HDCP_2_2_HDMI_RXSTATUS_LEN];
	int ret;

	ret = intel_hdmi_hdcp2_read_rx_status(intel_dig_port, rx_status);
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
int intel_hdmi_hdcp2_capable(struct intel_digital_port *intel_dig_port,
			     bool *capable)
{
	u8 hdcp2_version;
	int ret;

	*capable = false;
	ret = intel_hdmi_hdcp_read(intel_dig_port, HDCP_2_2_HDMI_REG_VER_OFFSET,
				   &hdcp2_version, sizeof(hdcp2_version));
	if (!ret && hdcp2_version & HDCP_2_2_HDMI_SUPPORT_MASK)
		*capable = true;

	return ret;
}

static inline
enum hdcp_wired_protocol intel_hdmi_hdcp2_protocol(void)
{
	return HDCP_PROTOCOL_HDMI;
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

static void intel_hdmi_prepare(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	const struct drm_display_mode *adjusted_mode = &crtc_state->base.adjusted_mode;
	u32 hdmi_val;

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, true);

	hdmi_val = SDVO_ENCODING_HDMI;
	if (!HAS_PCH_SPLIT(dev_priv) && crtc_state->limited_color_range)
		hdmi_val |= HDMI_COLOR_RANGE_16_235;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		hdmi_val |= SDVO_VSYNC_ACTIVE_HIGH;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		hdmi_val |= SDVO_HSYNC_ACTIVE_HIGH;

	if (crtc_state->pipe_bpp > 24)
		hdmi_val |= HDMI_COLOR_FORMAT_12bpc;
	else
		hdmi_val |= SDVO_COLOR_FORMAT_8bpc;

	if (crtc_state->has_hdmi_sink)
		hdmi_val |= HDMI_MODE_SELECT_HDMI;

	if (HAS_PCH_CPT(dev_priv))
		hdmi_val |= SDVO_PIPE_SEL_CPT(crtc->pipe);
	else if (IS_CHERRYVIEW(dev_priv))
		hdmi_val |= SDVO_PIPE_SEL_CHV(crtc->pipe);
	else
		hdmi_val |= SDVO_PIPE_SEL(crtc->pipe);

	I915_WRITE(intel_hdmi->hdmi_reg, hdmi_val);
	POSTING_READ(intel_hdmi->hdmi_reg);
}

static bool intel_hdmi_get_hw_state(struct intel_encoder *encoder,
				    enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	intel_wakeref_t wakeref;
	bool ret;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	ret = intel_sdvo_port_enabled(dev_priv, intel_hdmi->hdmi_reg, pipe);

	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);

	return ret;
}

static void intel_hdmi_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *pipe_config)
{
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 tmp, flags = 0;
	int dotclock;

	pipe_config->output_types |= BIT(INTEL_OUTPUT_HDMI);

	tmp = I915_READ(intel_hdmi->hdmi_reg);

	if (tmp & SDVO_HSYNC_ACTIVE_HIGH)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;

	if (tmp & SDVO_VSYNC_ACTIVE_HIGH)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;

	if (tmp & HDMI_MODE_SELECT_HDMI)
		pipe_config->has_hdmi_sink = true;

	pipe_config->infoframes.enable |=
		intel_hdmi_infoframes_enabled(encoder, pipe_config);

	if (pipe_config->infoframes.enable)
		pipe_config->has_infoframe = true;

	if (tmp & SDVO_AUDIO_ENABLE)
		pipe_config->has_audio = true;

	if (!HAS_PCH_SPLIT(dev_priv) &&
	    tmp & HDMI_COLOR_RANGE_16_235)
		pipe_config->limited_color_range = true;

	pipe_config->base.adjusted_mode.flags |= flags;

	if ((tmp & SDVO_COLOR_FORMAT_MASK) == HDMI_COLOR_FORMAT_12bpc)
		dotclock = pipe_config->port_clock * 2 / 3;
	else
		dotclock = pipe_config->port_clock;

	if (pipe_config->pixel_multiplier)
		dotclock /= pipe_config->pixel_multiplier;

	pipe_config->base.adjusted_mode.crtc_clock = dotclock;

	pipe_config->lane_count = 4;

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
}

static void intel_enable_hdmi_audio(struct intel_encoder *encoder,
				    const struct intel_crtc_state *pipe_config,
				    const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->base.crtc);

	WARN_ON(!pipe_config->has_hdmi_sink);
	DRM_DEBUG_DRIVER("Enabling HDMI audio on pipe %c\n",
			 pipe_name(crtc->pipe));
	intel_audio_codec_enable(encoder, pipe_config, conn_state);
}

static void g4x_enable_hdmi(struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	u32 temp;

	temp = I915_READ(intel_hdmi->hdmi_reg);

	temp |= SDVO_ENABLE;
	if (pipe_config->has_audio)
		temp |= SDVO_AUDIO_ENABLE;

	I915_WRITE(intel_hdmi->hdmi_reg, temp);
	POSTING_READ(intel_hdmi->hdmi_reg);

	if (pipe_config->has_audio)
		intel_enable_hdmi_audio(encoder, pipe_config, conn_state);
}

static void ibx_enable_hdmi(struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	u32 temp;

	temp = I915_READ(intel_hdmi->hdmi_reg);

	temp |= SDVO_ENABLE;
	if (pipe_config->has_audio)
		temp |= SDVO_AUDIO_ENABLE;

	/*
	 * HW workaround, need to write this twice for issue
	 * that may result in first write getting masked.
	 */
	I915_WRITE(intel_hdmi->hdmi_reg, temp);
	POSTING_READ(intel_hdmi->hdmi_reg);
	I915_WRITE(intel_hdmi->hdmi_reg, temp);
	POSTING_READ(intel_hdmi->hdmi_reg);

	/*
	 * HW workaround, need to toggle enable bit off and on
	 * for 12bpc with pixel repeat.
	 *
	 * FIXME: BSpec says this should be done at the end of
	 * of the modeset sequence, so not sure if this isn't too soon.
	 */
	if (pipe_config->pipe_bpp > 24 &&
	    pipe_config->pixel_multiplier > 1) {
		I915_WRITE(intel_hdmi->hdmi_reg, temp & ~SDVO_ENABLE);
		POSTING_READ(intel_hdmi->hdmi_reg);

		/*
		 * HW workaround, need to write this twice for issue
		 * that may result in first write getting masked.
		 */
		I915_WRITE(intel_hdmi->hdmi_reg, temp);
		POSTING_READ(intel_hdmi->hdmi_reg);
		I915_WRITE(intel_hdmi->hdmi_reg, temp);
		POSTING_READ(intel_hdmi->hdmi_reg);
	}

	if (pipe_config->has_audio)
		intel_enable_hdmi_audio(encoder, pipe_config, conn_state);
}

static void cpt_enable_hdmi(struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->base.crtc);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	enum pipe pipe = crtc->pipe;
	u32 temp;

	temp = I915_READ(intel_hdmi->hdmi_reg);

	temp |= SDVO_ENABLE;
	if (pipe_config->has_audio)
		temp |= SDVO_AUDIO_ENABLE;

	/*
	 * WaEnableHDMI8bpcBefore12bpc:snb,ivb
	 *
	 * The procedure for 12bpc is as follows:
	 * 1. disable HDMI clock gating
	 * 2. enable HDMI with 8bpc
	 * 3. enable HDMI with 12bpc
	 * 4. enable HDMI clock gating
	 */

	if (pipe_config->pipe_bpp > 24) {
		I915_WRITE(TRANS_CHICKEN1(pipe),
			   I915_READ(TRANS_CHICKEN1(pipe)) |
			   TRANS_CHICKEN1_HDMIUNIT_GC_DISABLE);

		temp &= ~SDVO_COLOR_FORMAT_MASK;
		temp |= SDVO_COLOR_FORMAT_8bpc;
	}

	I915_WRITE(intel_hdmi->hdmi_reg, temp);
	POSTING_READ(intel_hdmi->hdmi_reg);

	if (pipe_config->pipe_bpp > 24) {
		temp &= ~SDVO_COLOR_FORMAT_MASK;
		temp |= HDMI_COLOR_FORMAT_12bpc;

		I915_WRITE(intel_hdmi->hdmi_reg, temp);
		POSTING_READ(intel_hdmi->hdmi_reg);

		I915_WRITE(TRANS_CHICKEN1(pipe),
			   I915_READ(TRANS_CHICKEN1(pipe)) &
			   ~TRANS_CHICKEN1_HDMIUNIT_GC_DISABLE);
	}

	if (pipe_config->has_audio)
		intel_enable_hdmi_audio(encoder, pipe_config, conn_state);
}

static void vlv_enable_hdmi(struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
}

static void intel_disable_hdmi(struct intel_encoder *encoder,
			       const struct intel_crtc_state *old_crtc_state,
			       const struct drm_connector_state *old_conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	struct intel_digital_port *intel_dig_port =
		hdmi_to_dig_port(intel_hdmi);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->base.crtc);
	u32 temp;

	temp = I915_READ(intel_hdmi->hdmi_reg);

	temp &= ~(SDVO_ENABLE | SDVO_AUDIO_ENABLE);
	I915_WRITE(intel_hdmi->hdmi_reg, temp);
	POSTING_READ(intel_hdmi->hdmi_reg);

	/*
	 * HW workaround for IBX, we need to move the port
	 * to transcoder A after disabling it to allow the
	 * matching DP port to be enabled on transcoder A.
	 */
	if (HAS_PCH_IBX(dev_priv) && crtc->pipe == PIPE_B) {
		/*
		 * We get CPU/PCH FIFO underruns on the other pipe when
		 * doing the workaround. Sweep them under the rug.
		 */
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, false);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, false);

		temp &= ~SDVO_PIPE_SEL_MASK;
		temp |= SDVO_ENABLE | SDVO_PIPE_SEL(PIPE_A);
		/*
		 * HW workaround, need to write this twice for issue
		 * that may result in first write getting masked.
		 */
		I915_WRITE(intel_hdmi->hdmi_reg, temp);
		POSTING_READ(intel_hdmi->hdmi_reg);
		I915_WRITE(intel_hdmi->hdmi_reg, temp);
		POSTING_READ(intel_hdmi->hdmi_reg);

		temp &= ~SDVO_ENABLE;
		I915_WRITE(intel_hdmi->hdmi_reg, temp);
		POSTING_READ(intel_hdmi->hdmi_reg);

		intel_wait_for_vblank_if_active(dev_priv, PIPE_A);
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, true);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, true);
	}

	intel_dig_port->set_infoframes(encoder,
				       false,
				       old_crtc_state, old_conn_state);

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, false);
}

static void g4x_disable_hdmi(struct intel_encoder *encoder,
			     const struct intel_crtc_state *old_crtc_state,
			     const struct drm_connector_state *old_conn_state)
{
	if (old_crtc_state->has_audio)
		intel_audio_codec_disable(encoder,
					  old_crtc_state, old_conn_state);

	intel_disable_hdmi(encoder, old_crtc_state, old_conn_state);
}

static void pch_disable_hdmi(struct intel_encoder *encoder,
			     const struct intel_crtc_state *old_crtc_state,
			     const struct drm_connector_state *old_conn_state)
{
	if (old_crtc_state->has_audio)
		intel_audio_codec_disable(encoder,
					  old_crtc_state, old_conn_state);
}

static void pch_post_disable_hdmi(struct intel_encoder *encoder,
				  const struct intel_crtc_state *old_crtc_state,
				  const struct drm_connector_state *old_conn_state)
{
	intel_disable_hdmi(encoder, old_crtc_state, old_conn_state);
}

static int intel_hdmi_source_max_tmds_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	const struct ddi_vbt_port_info *info =
		&dev_priv->vbt.ddi_port_info[encoder->port];
	int max_tmds_clock;

	if (INTEL_GEN(dev_priv) >= 10 || IS_GEMINILAKE(dev_priv))
		max_tmds_clock = 594000;
	else if (INTEL_GEN(dev_priv) >= 8 || IS_HASWELL(dev_priv))
		max_tmds_clock = 300000;
	else if (INTEL_GEN(dev_priv) >= 5)
		max_tmds_clock = 225000;
	else
		max_tmds_clock = 165000;

	if (info->max_tmds_clock)
		max_tmds_clock = min(max_tmds_clock, info->max_tmds_clock);

	return max_tmds_clock;
}

static int hdmi_port_clock_limit(struct intel_hdmi *hdmi,
				 bool respect_downstream_limits,
				 bool force_dvi)
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
		else if (!hdmi->has_hdmi_sink || force_dvi)
			max_tmds_clock = min(max_tmds_clock, 165000);
	}

	return max_tmds_clock;
}

static enum drm_mode_status
hdmi_port_clock_valid(struct intel_hdmi *hdmi,
		      int clock, bool respect_downstream_limits,
		      bool force_dvi)
{
	struct drm_i915_private *dev_priv = to_i915(intel_hdmi_to_dev(hdmi));

	if (clock < 25000)
		return MODE_CLOCK_LOW;
	if (clock > hdmi_port_clock_limit(hdmi, respect_downstream_limits, force_dvi))
		return MODE_CLOCK_HIGH;

	/* BXT DPLL can't generate 223-240 MHz */
	if (IS_GEN9_LP(dev_priv) && clock > 223333 && clock < 240000)
		return MODE_CLOCK_RANGE;

	/* CHV DPLL can't generate 216-240 MHz */
	if (IS_CHERRYVIEW(dev_priv) && clock > 216000 && clock < 240000)
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static enum drm_mode_status
intel_hdmi_mode_valid(struct drm_connector *connector,
		      struct drm_display_mode *mode)
{
	struct intel_hdmi *hdmi = intel_attached_hdmi(connector);
	struct drm_device *dev = intel_hdmi_to_dev(hdmi);
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum drm_mode_status status;
	int clock;
	int max_dotclk = to_i915(connector->dev)->max_dotclk_freq;
	bool force_dvi =
		READ_ONCE(to_intel_digital_connector_state(connector->state)->force_audio) == HDMI_AUDIO_OFF_DVI;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	clock = mode->clock;

	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) == DRM_MODE_FLAG_3D_FRAME_PACKING)
		clock *= 2;

	if (clock > max_dotclk)
		return MODE_CLOCK_HIGH;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		clock *= 2;

	if (drm_mode_is_420_only(&connector->display_info, mode))
		clock /= 2;

	/* check if we can do 8bpc */
	status = hdmi_port_clock_valid(hdmi, clock, true, force_dvi);

	if (hdmi->has_hdmi_sink && !force_dvi) {
		/* if we can't do 8bpc we may still be able to do 12bpc */
		if (status != MODE_OK && !HAS_GMCH(dev_priv))
			status = hdmi_port_clock_valid(hdmi, clock * 3 / 2,
						       true, force_dvi);

		/* if we can't do 8,12bpc we may still be able to do 10bpc */
		if (status != MODE_OK && INTEL_GEN(dev_priv) >= 11)
			status = hdmi_port_clock_valid(hdmi, clock * 5 / 4,
						       true, force_dvi);
	}

	return status;
}

static bool hdmi_deep_color_possible(const struct intel_crtc_state *crtc_state,
				     int bpc)
{
	struct drm_i915_private *dev_priv =
		to_i915(crtc_state->base.crtc->dev);
	struct drm_atomic_state *state = crtc_state->base.state;
	struct drm_connector_state *connector_state;
	struct drm_connector *connector;
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;
	int i;

	if (HAS_GMCH(dev_priv))
		return false;

	if (bpc == 10 && INTEL_GEN(dev_priv) < 11)
		return false;

	if (crtc_state->pipe_bpp <= 8*3)
		return false;

	if (!crtc_state->has_hdmi_sink)
		return false;

	/*
	 * HDMI deep color affects the clocks, so it's only possible
	 * when not cloning with other encoder types.
	 */
	if (crtc_state->output_types != 1 << INTEL_OUTPUT_HDMI)
		return false;

	for_each_new_connector_in_state(state, connector, connector_state, i) {
		const struct drm_display_info *info = &connector->display_info;

		if (connector_state->crtc != crtc_state->base.crtc)
			continue;

		if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420) {
			const struct drm_hdmi_info *hdmi = &info->hdmi;

			if (bpc == 12 && !(hdmi->y420_dc_modes &
					   DRM_EDID_YCBCR420_DC_36))
				return false;
			else if (bpc == 10 && !(hdmi->y420_dc_modes &
						DRM_EDID_YCBCR420_DC_30))
				return false;
		} else {
			if (bpc == 12 && !(info->edid_hdmi_dc_modes &
					   DRM_EDID_HDMI_DC_36))
				return false;
			else if (bpc == 10 && !(info->edid_hdmi_dc_modes &
						DRM_EDID_HDMI_DC_30))
				return false;
		}
	}

	/* Display WA #1139: glk */
	if (bpc == 12 && IS_GLK_REVID(dev_priv, 0, GLK_REVID_A1) &&
	    adjusted_mode->htotal > 5460)
		return false;

	/* Display Wa_1405510057:icl */
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420 &&
	    bpc == 10 && INTEL_GEN(dev_priv) >= 11 &&
	    (adjusted_mode->crtc_hblank_end -
	     adjusted_mode->crtc_hblank_start) % 8 == 2)
		return false;

	return true;
}

static bool
intel_hdmi_ycbcr420_config(struct drm_connector *connector,
			   struct intel_crtc_state *config,
			   int *clock_12bpc, int *clock_10bpc,
			   int *clock_8bpc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(config->base.crtc);

	if (!connector->ycbcr_420_allowed) {
		DRM_ERROR("Platform doesn't support YCBCR420 output\n");
		return false;
	}

	/* YCBCR420 TMDS rate requirement is half the pixel clock */
	config->port_clock /= 2;
	*clock_12bpc /= 2;
	*clock_10bpc /= 2;
	*clock_8bpc /= 2;
	config->output_format = INTEL_OUTPUT_FORMAT_YCBCR420;

	/* YCBCR 420 output conversion needs a scaler */
	if (skl_update_scaler_crtc(config)) {
		DRM_DEBUG_KMS("Scaler allocation for output failed\n");
		return false;
	}

	intel_pch_panel_fitting(intel_crtc, config,
				DRM_MODE_SCALE_FULLSCREEN);

	return true;
}

int intel_hdmi_compute_config(struct intel_encoder *encoder,
			      struct intel_crtc_state *pipe_config,
			      struct drm_connector_state *conn_state)
{
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	struct drm_connector *connector = conn_state->connector;
	struct drm_scdc *scdc = &connector->display_info.hdmi.scdc;
	struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(conn_state);
	int clock_8bpc = pipe_config->base.adjusted_mode.crtc_clock;
	int clock_10bpc = clock_8bpc * 5 / 4;
	int clock_12bpc = clock_8bpc * 3 / 2;
	int desired_bpp;
	bool force_dvi = intel_conn_state->force_audio == HDMI_AUDIO_OFF_DVI;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->has_hdmi_sink = !force_dvi && intel_hdmi->has_hdmi_sink;

	if (pipe_config->has_hdmi_sink)
		pipe_config->has_infoframe = true;

	if (intel_conn_state->broadcast_rgb == INTEL_BROADCAST_RGB_AUTO) {
		/* See CEA-861-E - 5.1 Default Encoding Parameters */
		pipe_config->limited_color_range =
			pipe_config->has_hdmi_sink &&
			drm_default_rgb_quant_range(adjusted_mode) ==
			HDMI_QUANTIZATION_RANGE_LIMITED;
	} else {
		pipe_config->limited_color_range =
			intel_conn_state->broadcast_rgb == INTEL_BROADCAST_RGB_LIMITED;
	}

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLCLK) {
		pipe_config->pixel_multiplier = 2;
		clock_8bpc *= 2;
		clock_10bpc *= 2;
		clock_12bpc *= 2;
	}

	if (drm_mode_is_420_only(&connector->display_info, adjusted_mode)) {
		if (!intel_hdmi_ycbcr420_config(connector, pipe_config,
						&clock_12bpc, &clock_10bpc,
						&clock_8bpc)) {
			DRM_ERROR("Can't support YCBCR420 output\n");
			return -EINVAL;
		}
	}

	if (HAS_PCH_SPLIT(dev_priv) && !HAS_DDI(dev_priv))
		pipe_config->has_pch_encoder = true;

	if (pipe_config->has_hdmi_sink) {
		if (intel_conn_state->force_audio == HDMI_AUDIO_AUTO)
			pipe_config->has_audio = intel_hdmi->has_audio;
		else
			pipe_config->has_audio =
				intel_conn_state->force_audio == HDMI_AUDIO_ON;
	}

	/*
	 * Note that g4x/vlv don't support 12bpc hdmi outputs. We also need
	 * to check that the higher clock still fits within limits.
	 */
	if (hdmi_deep_color_possible(pipe_config, 12) &&
	    hdmi_port_clock_valid(intel_hdmi, clock_12bpc,
				  true, force_dvi) == MODE_OK) {
		DRM_DEBUG_KMS("picking bpc to 12 for HDMI output\n");
		desired_bpp = 12*3;

		/* Need to adjust the port link by 1.5x for 12bpc. */
		pipe_config->port_clock = clock_12bpc;
	} else if (hdmi_deep_color_possible(pipe_config, 10) &&
		   hdmi_port_clock_valid(intel_hdmi, clock_10bpc,
					 true, force_dvi) == MODE_OK) {
		DRM_DEBUG_KMS("picking bpc to 10 for HDMI output\n");
		desired_bpp = 10 * 3;

		/* Need to adjust the port link by 1.25x for 10bpc. */
		pipe_config->port_clock = clock_10bpc;
	} else {
		DRM_DEBUG_KMS("picking bpc to 8 for HDMI output\n");
		desired_bpp = 8*3;

		pipe_config->port_clock = clock_8bpc;
	}

	if (!pipe_config->bw_constrained) {
		DRM_DEBUG_KMS("forcing pipe bpp to %i for HDMI\n", desired_bpp);
		pipe_config->pipe_bpp = desired_bpp;
	}

	if (hdmi_port_clock_valid(intel_hdmi, pipe_config->port_clock,
				  false, force_dvi) != MODE_OK) {
		DRM_DEBUG_KMS("unsupported HDMI clock, rejecting mode\n");
		return -EINVAL;
	}

	/* Set user selected PAR to incoming mode's member */
	adjusted_mode->picture_aspect_ratio = conn_state->picture_aspect_ratio;

	pipe_config->lane_count = 4;

	if (scdc->scrambling.supported && (INTEL_GEN(dev_priv) >= 10 ||
					   IS_GEMINILAKE(dev_priv))) {
		if (scdc->scrambling.low_rates)
			pipe_config->hdmi_scrambling = true;

		if (pipe_config->port_clock > 340000) {
			pipe_config->hdmi_scrambling = true;
			pipe_config->hdmi_high_tmds_clock_ratio = true;
		}
	}

	intel_hdmi_compute_gcp_infoframe(encoder, pipe_config, conn_state);

	if (!intel_hdmi_compute_avi_infoframe(encoder, pipe_config, conn_state)) {
		DRM_DEBUG_KMS("bad AVI infoframe\n");
		return -EINVAL;
	}

	if (!intel_hdmi_compute_spd_infoframe(encoder, pipe_config, conn_state)) {
		DRM_DEBUG_KMS("bad SPD infoframe\n");
		return -EINVAL;
	}

	if (!intel_hdmi_compute_hdmi_infoframe(encoder, pipe_config, conn_state)) {
		DRM_DEBUG_KMS("bad HDMI infoframe\n");
		return -EINVAL;
	}

	return 0;
}

static void
intel_hdmi_unset_edid(struct drm_connector *connector)
{
	struct intel_hdmi *intel_hdmi = intel_attached_hdmi(connector);

	intel_hdmi->has_hdmi_sink = false;
	intel_hdmi->has_audio = false;

	intel_hdmi->dp_dual_mode.type = DRM_DP_DUAL_MODE_NONE;
	intel_hdmi->dp_dual_mode.max_tmds_clock = 0;

	kfree(to_intel_connector(connector)->detect_edid);
	to_intel_connector(connector)->detect_edid = NULL;
}

static void
intel_hdmi_dp_dual_mode_detect(struct drm_connector *connector, bool has_edid)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_hdmi *hdmi = intel_attached_hdmi(connector);
	enum port port = hdmi_to_dig_port(hdmi)->base.port;
	struct i2c_adapter *adapter =
		intel_gmbus_get_adapter(dev_priv, hdmi->ddc_bus);
	enum drm_dp_dual_mode_type type = drm_dp_dual_mode_detect(adapter);

	/*
	 * Type 1 DVI adaptors are not required to implement any
	 * registers, so we can't always detect their presence.
	 * Ideally we should be able to check the state of the
	 * CONFIG1 pin, but no such luck on our hardware.
	 *
	 * The only method left to us is to check the VBT to see
	 * if the port is a dual mode capable DP port. But let's
	 * only do that when we sucesfully read the EDID, to avoid
	 * confusing log messages about DP dual mode adaptors when
	 * there's nothing connected to the port.
	 */
	if (type == DRM_DP_DUAL_MODE_UNKNOWN) {
		/* An overridden EDID imply that we want this port for testing.
		 * Make sure not to set limits for that port.
		 */
		if (has_edid && !connector->override_edid &&
		    intel_bios_is_port_dp_dual_mode(dev_priv, port)) {
			DRM_DEBUG_KMS("Assuming DP dual mode adaptor presence based on VBT\n");
			type = DRM_DP_DUAL_MODE_TYPE1_DVI;
		} else {
			type = DRM_DP_DUAL_MODE_NONE;
		}
	}

	if (type == DRM_DP_DUAL_MODE_NONE)
		return;

	hdmi->dp_dual_mode.type = type;
	hdmi->dp_dual_mode.max_tmds_clock =
		drm_dp_dual_mode_max_tmds_clock(type, adapter);

	DRM_DEBUG_KMS("DP dual mode adaptor (%s) detected (max TMDS clock: %d kHz)\n",
		      drm_dp_get_dual_mode_type_name(type),
		      hdmi->dp_dual_mode.max_tmds_clock);
}

static bool
intel_hdmi_set_edid(struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_hdmi *intel_hdmi = intel_attached_hdmi(connector);
	intel_wakeref_t wakeref;
	struct edid *edid;
	bool connected = false;
	struct i2c_adapter *i2c;

	wakeref = intel_display_power_get(dev_priv, POWER_DOMAIN_GMBUS);

	i2c = intel_gmbus_get_adapter(dev_priv, intel_hdmi->ddc_bus);

	edid = drm_get_edid(connector, i2c);

	if (!edid && !intel_gmbus_is_forced_bit(i2c)) {
		DRM_DEBUG_KMS("HDMI GMBUS EDID read failed, retry using GPIO bit-banging\n");
		intel_gmbus_force_bit(i2c, true);
		edid = drm_get_edid(connector, i2c);
		intel_gmbus_force_bit(i2c, false);
	}

	intel_hdmi_dp_dual_mode_detect(connector, edid != NULL);

	intel_display_power_put(dev_priv, POWER_DOMAIN_GMBUS, wakeref);

	to_intel_connector(connector)->detect_edid = edid;
	if (edid && edid->input & DRM_EDID_INPUT_DIGITAL) {
		intel_hdmi->has_audio = drm_detect_monitor_audio(edid);
		intel_hdmi->has_hdmi_sink = drm_detect_hdmi_monitor(edid);

		connected = true;
	}

	cec_notifier_set_phys_addr_from_edid(intel_hdmi->cec_notifier, edid);

	return connected;
}

static enum drm_connector_status
intel_hdmi_detect(struct drm_connector *connector, bool force)
{
	enum drm_connector_status status = connector_status_disconnected;
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_hdmi *intel_hdmi = intel_attached_hdmi(connector);
	struct intel_encoder *encoder = &hdmi_to_dig_port(intel_hdmi)->base;
	intel_wakeref_t wakeref;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);

	wakeref = intel_display_power_get(dev_priv, POWER_DOMAIN_GMBUS);

	if (INTEL_GEN(dev_priv) >= 11 &&
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
	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);

	intel_hdmi_unset_edid(connector);

	if (connector->status != connector_status_connected)
		return;

	intel_hdmi_set_edid(connector);
}

static int intel_hdmi_get_modes(struct drm_connector *connector)
{
	struct edid *edid;

	edid = to_intel_connector(connector)->detect_edid;
	if (edid == NULL)
		return 0;

	return intel_connector_update_modes(connector, edid);
}

static void intel_hdmi_pre_enable(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config,
				  const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *intel_dig_port =
		enc_to_dig_port(&encoder->base);

	intel_hdmi_prepare(encoder, pipe_config);

	intel_dig_port->set_infoframes(encoder,
				       pipe_config->has_infoframe,
				       pipe_config, conn_state);
}

static void vlv_hdmi_pre_enable(struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config,
				const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	vlv_phy_pre_encoder_enable(encoder, pipe_config);

	/* HDMI 1.0V-2dB */
	vlv_set_phy_signal_level(encoder, 0x2b245f5f, 0x00002000, 0x5578b83a,
				 0x2b247878);

	dport->set_infoframes(encoder,
			      pipe_config->has_infoframe,
			      pipe_config, conn_state);

	g4x_enable_hdmi(encoder, pipe_config, conn_state);

	vlv_wait_port_ready(dev_priv, dport, 0x0);
}

static void vlv_hdmi_pre_pll_enable(struct intel_encoder *encoder,
				    const struct intel_crtc_state *pipe_config,
				    const struct drm_connector_state *conn_state)
{
	intel_hdmi_prepare(encoder, pipe_config);

	vlv_phy_pre_pll_enable(encoder, pipe_config);
}

static void chv_hdmi_pre_pll_enable(struct intel_encoder *encoder,
				    const struct intel_crtc_state *pipe_config,
				    const struct drm_connector_state *conn_state)
{
	intel_hdmi_prepare(encoder, pipe_config);

	chv_phy_pre_pll_enable(encoder, pipe_config);
}

static void chv_hdmi_post_pll_disable(struct intel_encoder *encoder,
				      const struct intel_crtc_state *old_crtc_state,
				      const struct drm_connector_state *old_conn_state)
{
	chv_phy_post_pll_disable(encoder, old_crtc_state);
}

static void vlv_hdmi_post_disable(struct intel_encoder *encoder,
				  const struct intel_crtc_state *old_crtc_state,
				  const struct drm_connector_state *old_conn_state)
{
	/* Reset lanes to avoid HDMI flicker (VLV w/a) */
	vlv_phy_reset_lanes(encoder, old_crtc_state);
}

static void chv_hdmi_post_disable(struct intel_encoder *encoder,
				  const struct intel_crtc_state *old_crtc_state,
				  const struct drm_connector_state *old_conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	mutex_lock(&dev_priv->sb_lock);

	/* Assert data lane reset */
	chv_data_lane_soft_reset(encoder, old_crtc_state, true);

	mutex_unlock(&dev_priv->sb_lock);
}

static void chv_hdmi_pre_enable(struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config,
				const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	chv_phy_pre_encoder_enable(encoder, pipe_config);

	/* FIXME: Program the support xxx V-dB */
	/* Use 800mV-0dB */
	chv_set_phy_signal_level(encoder, 128, 102, false);

	dport->set_infoframes(encoder,
			      pipe_config->has_infoframe,
			      pipe_config, conn_state);

	g4x_enable_hdmi(encoder, pipe_config, conn_state);

	vlv_wait_port_ready(dev_priv, dport, 0x0);

	/* Second common lane will stay alive on its own now */
	chv_phy_release_cl2_override(encoder);
}

static int
intel_hdmi_connector_register(struct drm_connector *connector)
{
	int ret;

	ret = intel_connector_register(connector);
	if (ret)
		return ret;

	i915_debugfs_connector_add(connector);

	return ret;
}

static void intel_hdmi_destroy(struct drm_connector *connector)
{
	if (intel_attached_hdmi(connector)->cec_notifier)
		cec_notifier_put(intel_attached_hdmi(connector)->cec_notifier);

	intel_connector_destroy(connector);
}

static const struct drm_connector_funcs intel_hdmi_connector_funcs = {
	.detect = intel_hdmi_detect,
	.force = intel_hdmi_force,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_get_property = intel_digital_connector_atomic_get_property,
	.atomic_set_property = intel_digital_connector_atomic_set_property,
	.late_register = intel_hdmi_connector_register,
	.early_unregister = intel_connector_unregister,
	.destroy = intel_hdmi_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_digital_connector_duplicate_state,
};

static const struct drm_connector_helper_funcs intel_hdmi_connector_helper_funcs = {
	.get_modes = intel_hdmi_get_modes,
	.mode_valid = intel_hdmi_mode_valid,
	.atomic_check = intel_digital_connector_atomic_check,
};

static const struct drm_encoder_funcs intel_hdmi_enc_funcs = {
	.destroy = intel_encoder_destroy,
};

static void
intel_hdmi_add_properties(struct intel_hdmi *intel_hdmi, struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_digital_port *intel_dig_port =
				hdmi_to_dig_port(intel_hdmi);

	intel_attach_force_audio_property(connector);
	intel_attach_broadcast_rgb_property(connector);
	intel_attach_aspect_ratio_property(connector);

	/*
	 * Attach Colorspace property for Non LSPCON based device
	 * ToDo: This needs to be extended for LSPCON implementation
	 * as well. Will be implemented separately.
	 */
	if (!intel_dig_port->lspcon.active)
		intel_attach_colorspace_property(connector);

	drm_connector_attach_content_type_property(connector);
	connector->state->picture_aspect_ratio = HDMI_PICTURE_ASPECT_NONE;

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
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&encoder->base);
	struct drm_scrambling *sink_scrambling =
		&connector->display_info.hdmi.scdc.scrambling;
	struct i2c_adapter *adapter =
		intel_gmbus_get_adapter(dev_priv, intel_hdmi->ddc_bus);

	if (!sink_scrambling->supported)
		return true;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] scrambling=%s, TMDS bit clock ratio=1/%d\n",
		      connector->base.id, connector->name,
		      yesno(scrambling), high_tmds_clock_ratio ? 40 : 10);

	/* Set TMDS bit clock ratio to 1/40 or 1/10, and enable/disable scrambling */
	return drm_scdc_set_high_tmds_clock_ratio(adapter,
						  high_tmds_clock_ratio) &&
		drm_scdc_set_scrambling(adapter, scrambling);
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
	u8 ddc_pin;

	switch (port) {
	case PORT_A:
		ddc_pin = GMBUS_PIN_1_BXT;
		break;
	case PORT_B:
		ddc_pin = GMBUS_PIN_2_BXT;
		break;
	case PORT_C:
		ddc_pin = GMBUS_PIN_9_TC1_ICP;
		break;
	case PORT_D:
		ddc_pin = GMBUS_PIN_10_TC2_ICP;
		break;
	case PORT_E:
		ddc_pin = GMBUS_PIN_11_TC3_ICP;
		break;
	case PORT_F:
		ddc_pin = GMBUS_PIN_12_TC4_ICP;
		break;
	default:
		MISSING_CASE(port);
		ddc_pin = GMBUS_PIN_2_BXT;
		break;
	}
	return ddc_pin;
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

static u8 intel_hdmi_ddc_pin(struct drm_i915_private *dev_priv,
			     enum port port)
{
	const struct ddi_vbt_port_info *info =
		&dev_priv->vbt.ddi_port_info[port];
	u8 ddc_pin;

	if (info->alternate_ddc_pin) {
		DRM_DEBUG_KMS("Using DDC pin 0x%x for port %c (VBT)\n",
			      info->alternate_ddc_pin, port_name(port));
		return info->alternate_ddc_pin;
	}

	if (HAS_PCH_ICP(dev_priv))
		ddc_pin = icl_port_to_ddc_pin(dev_priv, port);
	else if (HAS_PCH_CNP(dev_priv))
		ddc_pin = cnp_port_to_ddc_pin(dev_priv, port);
	else if (IS_GEN9_LP(dev_priv))
		ddc_pin = bxt_port_to_ddc_pin(dev_priv, port);
	else if (IS_CHERRYVIEW(dev_priv))
		ddc_pin = chv_port_to_ddc_pin(dev_priv, port);
	else
		ddc_pin = g4x_port_to_ddc_pin(dev_priv, port);

	DRM_DEBUG_KMS("Using DDC pin 0x%x for port %c (platform default)\n",
		      ddc_pin, port_name(port));

	return ddc_pin;
}

void intel_infoframe_init(struct intel_digital_port *intel_dig_port)
{
	struct drm_i915_private *dev_priv =
		to_i915(intel_dig_port->base.base.dev);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		intel_dig_port->write_infoframe = vlv_write_infoframe;
		intel_dig_port->read_infoframe = vlv_read_infoframe;
		intel_dig_port->set_infoframes = vlv_set_infoframes;
		intel_dig_port->infoframes_enabled = vlv_infoframes_enabled;
	} else if (IS_G4X(dev_priv)) {
		intel_dig_port->write_infoframe = g4x_write_infoframe;
		intel_dig_port->read_infoframe = g4x_read_infoframe;
		intel_dig_port->set_infoframes = g4x_set_infoframes;
		intel_dig_port->infoframes_enabled = g4x_infoframes_enabled;
	} else if (HAS_DDI(dev_priv)) {
		if (intel_dig_port->lspcon.active) {
			intel_dig_port->write_infoframe = lspcon_write_infoframe;
			intel_dig_port->read_infoframe = lspcon_read_infoframe;
			intel_dig_port->set_infoframes = lspcon_set_infoframes;
			intel_dig_port->infoframes_enabled = lspcon_infoframes_enabled;
		} else {
			intel_dig_port->write_infoframe = hsw_write_infoframe;
			intel_dig_port->read_infoframe = hsw_read_infoframe;
			intel_dig_port->set_infoframes = hsw_set_infoframes;
			intel_dig_port->infoframes_enabled = hsw_infoframes_enabled;
		}
	} else if (HAS_PCH_IBX(dev_priv)) {
		intel_dig_port->write_infoframe = ibx_write_infoframe;
		intel_dig_port->read_infoframe = ibx_read_infoframe;
		intel_dig_port->set_infoframes = ibx_set_infoframes;
		intel_dig_port->infoframes_enabled = ibx_infoframes_enabled;
	} else {
		intel_dig_port->write_infoframe = cpt_write_infoframe;
		intel_dig_port->read_infoframe = cpt_read_infoframe;
		intel_dig_port->set_infoframes = cpt_set_infoframes;
		intel_dig_port->infoframes_enabled = cpt_infoframes_enabled;
	}
}

void intel_hdmi_init_connector(struct intel_digital_port *intel_dig_port,
			       struct intel_connector *intel_connector)
{
	struct drm_connector *connector = &intel_connector->base;
	struct intel_hdmi *intel_hdmi = &intel_dig_port->hdmi;
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = intel_encoder->port;

	DRM_DEBUG_KMS("Adding HDMI connector on port %c\n",
		      port_name(port));

	if (WARN(intel_dig_port->max_lanes < 4,
		 "Not enough lanes (%d) for HDMI on port %c\n",
		 intel_dig_port->max_lanes, port_name(port)))
		return;

	drm_connector_init(dev, connector, &intel_hdmi_connector_funcs,
			   DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(connector, &intel_hdmi_connector_helper_funcs);

	connector->interlace_allowed = 1;
	connector->doublescan_allowed = 0;
	connector->stereo_allowed = 1;

	if (INTEL_GEN(dev_priv) >= 10 || IS_GEMINILAKE(dev_priv))
		connector->ycbcr_420_allowed = true;

	intel_hdmi->ddc_bus = intel_hdmi_ddc_pin(dev_priv, port);

	if (WARN_ON(port == PORT_A))
		return;
	intel_encoder->hpd_pin = intel_hpd_pin_default(dev_priv, port);

	if (HAS_DDI(dev_priv))
		intel_connector->get_hw_state = intel_ddi_connector_get_hw_state;
	else
		intel_connector->get_hw_state = intel_connector_get_hw_state;

	intel_hdmi_add_properties(intel_hdmi, connector);

	intel_connector_attach_encoder(intel_connector, intel_encoder);
	intel_hdmi->attached_connector = intel_connector;

	if (is_hdcp_supported(dev_priv, port)) {
		int ret = intel_hdcp_init(intel_connector,
					  &intel_hdmi_hdcp_shim);
		if (ret)
			DRM_DEBUG_KMS("HDCP init failed, skipping.\n");
	}

	/* For G4X desktop chip, PEG_BAND_GAP_DATA 3:0 must first be written
	 * 0xd.  Failure to do so will result in spurious interrupts being
	 * generated on the port when a cable is not attached.
	 */
	if (IS_G45(dev_priv)) {
		u32 temp = I915_READ(PEG_BAND_GAP_DATA);
		I915_WRITE(PEG_BAND_GAP_DATA, (temp & ~0xf) | 0xd);
	}

	intel_hdmi->cec_notifier = cec_notifier_get_conn(dev->dev,
							 port_identifier(port));
	if (!intel_hdmi->cec_notifier)
		DRM_DEBUG_KMS("CEC notifier get failed\n");
}

void intel_hdmi_init(struct drm_i915_private *dev_priv,
		     i915_reg_t hdmi_reg, enum port port)
{
	struct intel_digital_port *intel_dig_port;
	struct intel_encoder *intel_encoder;
	struct intel_connector *intel_connector;

	intel_dig_port = kzalloc(sizeof(*intel_dig_port), GFP_KERNEL);
	if (!intel_dig_port)
		return;

	intel_connector = intel_connector_alloc();
	if (!intel_connector) {
		kfree(intel_dig_port);
		return;
	}

	intel_encoder = &intel_dig_port->base;

	drm_encoder_init(&dev_priv->drm, &intel_encoder->base,
			 &intel_hdmi_enc_funcs, DRM_MODE_ENCODER_TMDS,
			 "HDMI %c", port_name(port));

	intel_encoder->hotplug = intel_encoder_hotplug;
	intel_encoder->compute_config = intel_hdmi_compute_config;
	if (HAS_PCH_SPLIT(dev_priv)) {
		intel_encoder->disable = pch_disable_hdmi;
		intel_encoder->post_disable = pch_post_disable_hdmi;
	} else {
		intel_encoder->disable = g4x_disable_hdmi;
	}
	intel_encoder->get_hw_state = intel_hdmi_get_hw_state;
	intel_encoder->get_config = intel_hdmi_get_config;
	if (IS_CHERRYVIEW(dev_priv)) {
		intel_encoder->pre_pll_enable = chv_hdmi_pre_pll_enable;
		intel_encoder->pre_enable = chv_hdmi_pre_enable;
		intel_encoder->enable = vlv_enable_hdmi;
		intel_encoder->post_disable = chv_hdmi_post_disable;
		intel_encoder->post_pll_disable = chv_hdmi_post_pll_disable;
	} else if (IS_VALLEYVIEW(dev_priv)) {
		intel_encoder->pre_pll_enable = vlv_hdmi_pre_pll_enable;
		intel_encoder->pre_enable = vlv_hdmi_pre_enable;
		intel_encoder->enable = vlv_enable_hdmi;
		intel_encoder->post_disable = vlv_hdmi_post_disable;
	} else {
		intel_encoder->pre_enable = intel_hdmi_pre_enable;
		if (HAS_PCH_CPT(dev_priv))
			intel_encoder->enable = cpt_enable_hdmi;
		else if (HAS_PCH_IBX(dev_priv))
			intel_encoder->enable = ibx_enable_hdmi;
		else
			intel_encoder->enable = g4x_enable_hdmi;
	}

	intel_encoder->type = INTEL_OUTPUT_HDMI;
	intel_encoder->power_domain = intel_port_to_power_domain(port);
	intel_encoder->port = port;
	if (IS_CHERRYVIEW(dev_priv)) {
		if (port == PORT_D)
			intel_encoder->crtc_mask = 1 << 2;
		else
			intel_encoder->crtc_mask = (1 << 0) | (1 << 1);
	} else {
		intel_encoder->crtc_mask = (1 << 0) | (1 << 1) | (1 << 2);
	}
	intel_encoder->cloneable = 1 << INTEL_OUTPUT_ANALOG;
	/*
	 * BSpec is unclear about HDMI+HDMI cloning on g4x, but it seems
	 * to work on real hardware. And since g4x can send infoframes to
	 * only one port anyway, nothing is lost by allowing it.
	 */
	if (IS_G4X(dev_priv))
		intel_encoder->cloneable |= 1 << INTEL_OUTPUT_HDMI;

	intel_dig_port->hdmi.hdmi_reg = hdmi_reg;
	intel_dig_port->dp.output_reg = INVALID_MMIO_REG;
	intel_dig_port->max_lanes = 4;

	intel_infoframe_init(intel_dig_port);

	intel_dig_port->aux_ch = intel_bios_port_aux_ch(dev_priv, port);
	intel_hdmi_init_connector(intel_dig_port, intel_connector);
}

/*
 * Copyright © 2016 Intel Corporation
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
 *
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_dual_mode_helper.h>
#include <drm/drm_edid.h>

#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_lspcon.h"
#include "intel_hdmi.h"

/* LSPCON OUI Vendor ID(signatures) */
#define LSPCON_VENDOR_PARADE_OUI 0x001CF8
#define LSPCON_VENDOR_MCA_OUI 0x0060AD

#define DPCD_MCA_LSPCON_HDR_STATUS	0x70003
#define DPCD_PARADE_LSPCON_HDR_STATUS	0x00511

/* AUX addresses to write MCA AVI IF */
#define LSPCON_MCA_AVI_IF_WRITE_OFFSET 0x5C0
#define LSPCON_MCA_AVI_IF_CTRL 0x5DF
#define  LSPCON_MCA_AVI_IF_KICKOFF (1 << 0)
#define  LSPCON_MCA_AVI_IF_HANDLED (1 << 1)

/* AUX addresses to write Parade AVI IF */
#define LSPCON_PARADE_AVI_IF_WRITE_OFFSET 0x516
#define LSPCON_PARADE_AVI_IF_CTRL 0x51E
#define  LSPCON_PARADE_AVI_IF_KICKOFF (1 << 7)
#define LSPCON_PARADE_AVI_IF_DATA_SIZE 32

static struct intel_dp *lspcon_to_intel_dp(struct intel_lspcon *lspcon)
{
	struct intel_digital_port *dig_port =
		container_of(lspcon, struct intel_digital_port, lspcon);

	return &dig_port->dp;
}

static const char *lspcon_mode_name(enum drm_lspcon_mode mode)
{
	switch (mode) {
	case DRM_LSPCON_MODE_PCON:
		return "PCON";
	case DRM_LSPCON_MODE_LS:
		return "LS";
	case DRM_LSPCON_MODE_INVALID:
		return "INVALID";
	default:
		MISSING_CASE(mode);
		return "INVALID";
	}
}

static bool lspcon_detect_vendor(struct intel_lspcon *lspcon)
{
	struct intel_dp *dp = lspcon_to_intel_dp(lspcon);
	struct drm_dp_dpcd_ident *ident;
	u32 vendor_oui;

	if (drm_dp_read_desc(&dp->aux, &dp->desc, drm_dp_is_branch(dp->dpcd))) {
		DRM_ERROR("Can't read description\n");
		return false;
	}

	ident = &dp->desc.ident;
	vendor_oui = (ident->oui[0] << 16) | (ident->oui[1] << 8) |
		      ident->oui[2];

	switch (vendor_oui) {
	case LSPCON_VENDOR_MCA_OUI:
		lspcon->vendor = LSPCON_VENDOR_MCA;
		DRM_DEBUG_KMS("Vendor: Mega Chips\n");
		break;

	case LSPCON_VENDOR_PARADE_OUI:
		lspcon->vendor = LSPCON_VENDOR_PARADE;
		DRM_DEBUG_KMS("Vendor: Parade Tech\n");
		break;

	default:
		DRM_ERROR("Invalid/Unknown vendor OUI\n");
		return false;
	}

	return true;
}

static u32 get_hdr_status_reg(struct intel_lspcon *lspcon)
{
	if (lspcon->vendor == LSPCON_VENDOR_MCA)
		return DPCD_MCA_LSPCON_HDR_STATUS;
	else
		return DPCD_PARADE_LSPCON_HDR_STATUS;
}

void lspcon_detect_hdr_capability(struct intel_lspcon *lspcon)
{
	struct intel_digital_port *dig_port =
		container_of(lspcon, struct intel_digital_port, lspcon);
	struct drm_device *dev = dig_port->base.base.dev;
	struct intel_dp *dp = lspcon_to_intel_dp(lspcon);
	u8 hdr_caps;
	int ret;

	ret = drm_dp_dpcd_read(&dp->aux, get_hdr_status_reg(lspcon),
			       &hdr_caps, 1);

	if (ret < 0) {
		drm_dbg_kms(dev, "HDR capability detection failed\n");
		lspcon->hdr_supported = false;
	} else if (hdr_caps & 0x1) {
		drm_dbg_kms(dev, "LSPCON capable of HDR\n");
		lspcon->hdr_supported = true;
	}
}

static enum drm_lspcon_mode lspcon_get_current_mode(struct intel_lspcon *lspcon)
{
	enum drm_lspcon_mode current_mode;
	struct i2c_adapter *adapter = &lspcon_to_intel_dp(lspcon)->aux.ddc;

	if (drm_lspcon_get_mode(adapter, &current_mode)) {
		DRM_DEBUG_KMS("Error reading LSPCON mode\n");
		return DRM_LSPCON_MODE_INVALID;
	}
	return current_mode;
}

static enum drm_lspcon_mode lspcon_wait_mode(struct intel_lspcon *lspcon,
					     enum drm_lspcon_mode mode)
{
	enum drm_lspcon_mode current_mode;

	current_mode = lspcon_get_current_mode(lspcon);
	if (current_mode == mode)
		goto out;

	DRM_DEBUG_KMS("Waiting for LSPCON mode %s to settle\n",
		      lspcon_mode_name(mode));

	wait_for((current_mode = lspcon_get_current_mode(lspcon)) == mode, 400);
	if (current_mode != mode)
		DRM_ERROR("LSPCON mode hasn't settled\n");

out:
	DRM_DEBUG_KMS("Current LSPCON mode %s\n",
		      lspcon_mode_name(current_mode));

	return current_mode;
}

static int lspcon_change_mode(struct intel_lspcon *lspcon,
			      enum drm_lspcon_mode mode)
{
	int err;
	enum drm_lspcon_mode current_mode;
	struct i2c_adapter *adapter = &lspcon_to_intel_dp(lspcon)->aux.ddc;

	err = drm_lspcon_get_mode(adapter, &current_mode);
	if (err) {
		DRM_ERROR("Error reading LSPCON mode\n");
		return err;
	}

	if (current_mode == mode) {
		DRM_DEBUG_KMS("Current mode = desired LSPCON mode\n");
		return 0;
	}

	err = drm_lspcon_set_mode(adapter, mode);
	if (err < 0) {
		DRM_ERROR("LSPCON mode change failed\n");
		return err;
	}

	lspcon->mode = mode;
	DRM_DEBUG_KMS("LSPCON mode changed done\n");
	return 0;
}

static bool lspcon_wake_native_aux_ch(struct intel_lspcon *lspcon)
{
	u8 rev;

	if (drm_dp_dpcd_readb(&lspcon_to_intel_dp(lspcon)->aux, DP_DPCD_REV,
			      &rev) != 1) {
		DRM_DEBUG_KMS("Native AUX CH down\n");
		return false;
	}

	DRM_DEBUG_KMS("Native AUX CH up, DPCD version: %d.%d\n",
		      rev >> 4, rev & 0xf);

	return true;
}

static bool lspcon_probe(struct intel_lspcon *lspcon)
{
	int retry;
	enum drm_dp_dual_mode_type adaptor_type;
	struct i2c_adapter *adapter = &lspcon_to_intel_dp(lspcon)->aux.ddc;
	enum drm_lspcon_mode expected_mode;

	expected_mode = lspcon_wake_native_aux_ch(lspcon) ?
			DRM_LSPCON_MODE_PCON : DRM_LSPCON_MODE_LS;

	/* Lets probe the adaptor and check its type */
	for (retry = 0; retry < 6; retry++) {
		if (retry)
			usleep_range(500, 1000);

		adaptor_type = drm_dp_dual_mode_detect(adapter);
		if (adaptor_type == DRM_DP_DUAL_MODE_LSPCON)
			break;
	}

	if (adaptor_type != DRM_DP_DUAL_MODE_LSPCON) {
		DRM_DEBUG_KMS("No LSPCON detected, found %s\n",
			       drm_dp_get_dual_mode_type_name(adaptor_type));
		return false;
	}

	/* Yay ... got a LSPCON device */
	DRM_DEBUG_KMS("LSPCON detected\n");
	lspcon->mode = lspcon_wait_mode(lspcon, expected_mode);

	/*
	 * In the SW state machine, lets Put LSPCON in PCON mode only.
	 * In this way, it will work with both HDMI 1.4 sinks as well as HDMI
	 * 2.0 sinks.
	 */
	if (lspcon->mode != DRM_LSPCON_MODE_PCON) {
		if (lspcon_change_mode(lspcon, DRM_LSPCON_MODE_PCON) < 0) {
			DRM_ERROR("LSPCON mode change to PCON failed\n");
			return false;
		}
	}
	return true;
}

static void lspcon_resume_in_pcon_wa(struct intel_lspcon *lspcon)
{
	struct intel_dp *intel_dp = lspcon_to_intel_dp(lspcon);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	unsigned long start = jiffies;

	while (1) {
		if (intel_digital_port_connected(&dig_port->base)) {
			DRM_DEBUG_KMS("LSPCON recovering in PCON mode after %u ms\n",
				      jiffies_to_msecs(jiffies - start));
			return;
		}

		if (time_after(jiffies, start + msecs_to_jiffies(1000)))
			break;

		usleep_range(10000, 15000);
	}

	DRM_DEBUG_KMS("LSPCON DP descriptor mismatch after resume\n");
}

static bool lspcon_parade_fw_ready(struct drm_dp_aux *aux)
{
	u8 avi_if_ctrl;
	u8 retry;
	ssize_t ret;

	/* Check if LSPCON FW is ready for data */
	for (retry = 0; retry < 5; retry++) {
		if (retry)
			usleep_range(200, 300);

		ret = drm_dp_dpcd_read(aux, LSPCON_PARADE_AVI_IF_CTRL,
				       &avi_if_ctrl, 1);
		if (ret < 0) {
			DRM_ERROR("Failed to read AVI IF control\n");
			return false;
		}

		if ((avi_if_ctrl & LSPCON_PARADE_AVI_IF_KICKOFF) == 0)
			return true;
	}

	DRM_ERROR("Parade FW not ready to accept AVI IF\n");
	return false;
}

static bool _lspcon_parade_write_infoframe_blocks(struct drm_dp_aux *aux,
						  u8 *avi_buf)
{
	u8 avi_if_ctrl;
	u8 block_count = 0;
	u8 *data;
	u16 reg;
	ssize_t ret;

	while (block_count < 4) {
		if (!lspcon_parade_fw_ready(aux)) {
			DRM_DEBUG_KMS("LSPCON FW not ready, block %d\n",
				      block_count);
			return false;
		}

		reg = LSPCON_PARADE_AVI_IF_WRITE_OFFSET;
		data = avi_buf + block_count * 8;
		ret = drm_dp_dpcd_write(aux, reg, data, 8);
		if (ret < 0) {
			DRM_ERROR("Failed to write AVI IF block %d\n",
				  block_count);
			return false;
		}

		/*
		 * Once a block of data is written, we have to inform the FW
		 * about this by writing into avi infoframe control register:
		 * - set the kickoff bit[7] to 1
		 * - write the block no. to bits[1:0]
		 */
		reg = LSPCON_PARADE_AVI_IF_CTRL;
		avi_if_ctrl = LSPCON_PARADE_AVI_IF_KICKOFF | block_count;
		ret = drm_dp_dpcd_write(aux, reg, &avi_if_ctrl, 1);
		if (ret < 0) {
			DRM_ERROR("Failed to update (0x%x), block %d\n",
				  reg, block_count);
			return false;
		}

		block_count++;
	}

	DRM_DEBUG_KMS("Wrote AVI IF blocks successfully\n");
	return true;
}

static bool _lspcon_write_avi_infoframe_parade(struct drm_dp_aux *aux,
					       const u8 *frame,
					       ssize_t len)
{
	u8 avi_if[LSPCON_PARADE_AVI_IF_DATA_SIZE] = {1, };

	/*
	 * Parade's frames contains 32 bytes of data, divided
	 * into 4 frames:
	 *	Token byte (first byte of first frame, must be non-zero)
	 *	HB0 to HB2	 from AVI IF (3 bytes header)
	 *	PB0 to PB27 from AVI IF (28 bytes data)
	 * So it should look like this
	 *	first block: | <token> <HB0-HB2> <DB0-DB3> |
	 *	next 3 blocks: |<DB4-DB11>|<DB12-DB19>|<DB20-DB28>|
	 */

	if (len > LSPCON_PARADE_AVI_IF_DATA_SIZE - 1) {
		DRM_ERROR("Invalid length of infoframes\n");
		return false;
	}

	memcpy(&avi_if[1], frame, len);

	if (!_lspcon_parade_write_infoframe_blocks(aux, avi_if)) {
		DRM_DEBUG_KMS("Failed to write infoframe blocks\n");
		return false;
	}

	return true;
}

static bool _lspcon_write_avi_infoframe_mca(struct drm_dp_aux *aux,
					    const u8 *buffer, ssize_t len)
{
	int ret;
	u32 val = 0;
	u32 retry;
	u16 reg;
	const u8 *data = buffer;

	reg = LSPCON_MCA_AVI_IF_WRITE_OFFSET;
	while (val < len) {
		/* DPCD write for AVI IF can fail on a slow FW day, so retry */
		for (retry = 0; retry < 5; retry++) {
			ret = drm_dp_dpcd_write(aux, reg, (void *)data, 1);
			if (ret == 1) {
				break;
			} else if (retry < 4) {
				mdelay(50);
				continue;
			} else {
				DRM_ERROR("DPCD write failed at:0x%x\n", reg);
				return false;
			}
		}
		val++; reg++; data++;
	}

	val = 0;
	reg = LSPCON_MCA_AVI_IF_CTRL;
	ret = drm_dp_dpcd_read(aux, reg, &val, 1);
	if (ret < 0) {
		DRM_ERROR("DPCD read failed, address 0x%x\n", reg);
		return false;
	}

	/* Indicate LSPCON chip about infoframe, clear bit 1 and set bit 0 */
	val &= ~LSPCON_MCA_AVI_IF_HANDLED;
	val |= LSPCON_MCA_AVI_IF_KICKOFF;

	ret = drm_dp_dpcd_write(aux, reg, &val, 1);
	if (ret < 0) {
		DRM_ERROR("DPCD read failed, address 0x%x\n", reg);
		return false;
	}

	val = 0;
	ret = drm_dp_dpcd_read(aux, reg, &val, 1);
	if (ret < 0) {
		DRM_ERROR("DPCD read failed, address 0x%x\n", reg);
		return false;
	}

	if (val == LSPCON_MCA_AVI_IF_HANDLED)
		DRM_DEBUG_KMS("AVI IF handled by FW\n");

	return true;
}

void lspcon_write_infoframe(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    unsigned int type,
			    const void *frame, ssize_t len)
{
	bool ret = true;
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_lspcon *lspcon = enc_to_intel_lspcon(encoder);

	switch (type) {
	case HDMI_INFOFRAME_TYPE_AVI:
		if (lspcon->vendor == LSPCON_VENDOR_MCA)
			ret = _lspcon_write_avi_infoframe_mca(&intel_dp->aux,
							      frame, len);
		else
			ret = _lspcon_write_avi_infoframe_parade(&intel_dp->aux,
								 frame, len);
		break;
	case HDMI_PACKET_TYPE_GAMUT_METADATA:
		drm_dbg_kms(encoder->base.dev, "Update HDR metadata for lspcon\n");
		/* It uses the legacy hsw implementation for the same */
		hsw_write_infoframe(encoder, crtc_state, type, frame, len);
		break;
	default:
		return;
	}

	if (!ret) {
		DRM_ERROR("Failed to write infoframes\n");
		return;
	}
}

void lspcon_read_infoframe(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   unsigned int type,
			   void *frame, ssize_t len)
{
	/* FIXME implement for AVI Infoframe as well */
	if (type == HDMI_PACKET_TYPE_GAMUT_METADATA)
		hsw_read_infoframe(encoder, crtc_state, type,
				   frame, len);
}

void lspcon_set_infoframes(struct intel_encoder *encoder,
			   bool enable,
			   const struct intel_crtc_state *crtc_state,
			   const struct drm_connector_state *conn_state)
{
	ssize_t ret;
	union hdmi_infoframe frame;
	u8 buf[VIDEO_DIP_DATA_SIZE];
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_lspcon *lspcon = &dig_port->lspcon;
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	if (!lspcon->active) {
		DRM_ERROR("Writing infoframes while LSPCON disabled ?\n");
		return;
	}

	/* FIXME precompute infoframes */

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						       conn_state->connector,
						       adjusted_mode);
	if (ret < 0) {
		DRM_ERROR("couldn't fill AVI infoframe\n");
		return;
	}

	/*
	 * Currently there is no interface defined to
	 * check user preference between RGB/YCBCR444
	 * or YCBCR420. So the only possible case for
	 * YCBCR444 usage is driving YCBCR420 output
	 * with LSPCON, when pipe is configured for
	 * YCBCR444 output and LSPCON takes care of
	 * downsampling it.
	 */
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV420;
	else
		frame.avi.colorspace = HDMI_COLORSPACE_RGB;

	/* Set the Colorspace as per the HDMI spec */
	drm_hdmi_avi_infoframe_colorspace(&frame.avi, conn_state);

	/* nonsense combination */
	drm_WARN_ON(encoder->base.dev, crtc_state->limited_color_range &&
		    crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB);

	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_RGB) {
		drm_hdmi_avi_infoframe_quant_range(&frame.avi,
						   conn_state->connector,
						   adjusted_mode,
						   crtc_state->limited_color_range ?
						   HDMI_QUANTIZATION_RANGE_LIMITED :
						   HDMI_QUANTIZATION_RANGE_FULL);
	} else {
		frame.avi.quantization_range = HDMI_QUANTIZATION_RANGE_DEFAULT;
		frame.avi.ycc_quantization_range = HDMI_YCC_QUANTIZATION_RANGE_LIMITED;
	}

	drm_hdmi_avi_infoframe_content_type(&frame.avi, conn_state);

	ret = hdmi_infoframe_pack(&frame, buf, sizeof(buf));
	if (ret < 0) {
		DRM_ERROR("Failed to pack AVI IF\n");
		return;
	}

	dig_port->write_infoframe(encoder, crtc_state, HDMI_INFOFRAME_TYPE_AVI,
				  buf, ret);
}

static bool _lspcon_read_avi_infoframe_enabled_mca(struct drm_dp_aux *aux)
{
	int ret;
	u32 val = 0;
	u16 reg = LSPCON_MCA_AVI_IF_CTRL;

	ret = drm_dp_dpcd_read(aux, reg, &val, 1);
	if (ret < 0) {
		DRM_ERROR("DPCD read failed, address 0x%x\n", reg);
		return false;
	}

	return val & LSPCON_MCA_AVI_IF_KICKOFF;
}

static bool _lspcon_read_avi_infoframe_enabled_parade(struct drm_dp_aux *aux)
{
	int ret;
	u32 val = 0;
	u16 reg = LSPCON_PARADE_AVI_IF_CTRL;

	ret = drm_dp_dpcd_read(aux, reg, &val, 1);
	if (ret < 0) {
		DRM_ERROR("DPCD read failed, address 0x%x\n", reg);
		return false;
	}

	return val & LSPCON_PARADE_AVI_IF_KICKOFF;
}

u32 lspcon_infoframes_enabled(struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_lspcon *lspcon = enc_to_intel_lspcon(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	bool infoframes_enabled;
	u32 val = 0;
	u32 mask, tmp;

	if (lspcon->vendor == LSPCON_VENDOR_MCA)
		infoframes_enabled = _lspcon_read_avi_infoframe_enabled_mca(&intel_dp->aux);
	else
		infoframes_enabled = _lspcon_read_avi_infoframe_enabled_parade(&intel_dp->aux);

	if (infoframes_enabled)
		val |= intel_hdmi_infoframe_enable(HDMI_INFOFRAME_TYPE_AVI);

	if (lspcon->hdr_supported) {
		tmp = intel_de_read(dev_priv,
				    HSW_TVIDEO_DIP_CTL(pipe_config->cpu_transcoder));
		mask = VIDEO_DIP_ENABLE_GMP_HSW;

		if (tmp & mask)
			val |= intel_hdmi_infoframe_enable(HDMI_PACKET_TYPE_GAMUT_METADATA);
	}

	return val;
}

void lspcon_wait_pcon_mode(struct intel_lspcon *lspcon)
{
	lspcon_wait_mode(lspcon, DRM_LSPCON_MODE_PCON);
}

bool lspcon_init(struct intel_digital_port *dig_port)
{
	struct intel_dp *dp = &dig_port->dp;
	struct intel_lspcon *lspcon = &dig_port->lspcon;
	struct drm_connector *connector = &dp->attached_connector->base;

	lspcon->active = false;
	lspcon->mode = DRM_LSPCON_MODE_INVALID;

	if (!lspcon_probe(lspcon)) {
		DRM_ERROR("Failed to probe lspcon\n");
		return false;
	}

	if (drm_dp_read_dpcd_caps(&dp->aux, dp->dpcd) != 0) {
		DRM_ERROR("LSPCON DPCD read failed\n");
		return false;
	}

	if (!lspcon_detect_vendor(lspcon)) {
		DRM_ERROR("LSPCON vendor detection failed\n");
		return false;
	}

	connector->ycbcr_420_allowed = true;
	lspcon->active = true;
	DRM_DEBUG_KMS("Success: LSPCON init\n");
	return true;
}

u32 intel_lspcon_infoframes_enabled(struct intel_encoder *encoder,
				    const struct intel_crtc_state *pipe_config)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	return dig_port->infoframes_enabled(encoder, pipe_config);
}

void lspcon_resume(struct intel_digital_port *dig_port)
{
	struct intel_lspcon *lspcon = &dig_port->lspcon;
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum drm_lspcon_mode expected_mode;

	if (!intel_bios_is_lspcon_present(dev_priv, dig_port->base.port))
		return;

	if (!lspcon->active) {
		if (!lspcon_init(dig_port)) {
			DRM_ERROR("LSPCON init failed on port %c\n",
				  port_name(dig_port->base.port));
			return;
		}
	}

	if (lspcon_wake_native_aux_ch(lspcon)) {
		expected_mode = DRM_LSPCON_MODE_PCON;
		lspcon_resume_in_pcon_wa(lspcon);
	} else {
		expected_mode = DRM_LSPCON_MODE_LS;
	}

	if (lspcon_wait_mode(lspcon, expected_mode) == DRM_LSPCON_MODE_PCON)
		return;

	if (lspcon_change_mode(lspcon, DRM_LSPCON_MODE_PCON))
		DRM_ERROR("LSPCON resume failed\n");
	else
		DRM_DEBUG_KMS("LSPCON resume success\n");
}

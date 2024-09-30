// SPDX-License-Identifier: MIT

#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>

#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>

/**
 * __drm_atomic_helper_connector_hdmi_reset() - Initializes all HDMI @drm_connector_state resources
 * @connector: DRM connector
 * @new_conn_state: connector state to reset
 *
 * Initializes all HDMI resources from a @drm_connector_state without
 * actually allocating it. This is useful for HDMI drivers, in
 * combination with __drm_atomic_helper_connector_reset() or
 * drm_atomic_helper_connector_reset().
 */
void __drm_atomic_helper_connector_hdmi_reset(struct drm_connector *connector,
					      struct drm_connector_state *new_conn_state)
{
	unsigned int max_bpc = connector->max_bpc;

	new_conn_state->max_bpc = max_bpc;
	new_conn_state->max_requested_bpc = max_bpc;
	new_conn_state->hdmi.broadcast_rgb = DRM_HDMI_BROADCAST_RGB_AUTO;
}
EXPORT_SYMBOL(__drm_atomic_helper_connector_hdmi_reset);

static const struct drm_display_mode *
connector_state_get_mode(const struct drm_connector_state *conn_state)
{
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;

	state = conn_state->state;
	if (!state)
		return NULL;

	crtc = conn_state->crtc;
	if (!crtc)
		return NULL;

	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (!crtc_state)
		return NULL;

	return &crtc_state->mode;
}

static bool hdmi_is_limited_range(const struct drm_connector *connector,
				  const struct drm_connector_state *conn_state)
{
	const struct drm_display_info *info = &connector->display_info;
	const struct drm_display_mode *mode =
		connector_state_get_mode(conn_state);

	/*
	 * The Broadcast RGB property only applies to RGB format, and
	 * i915 just assumes limited range for YCbCr output, so let's
	 * just do the same.
	 */
	if (conn_state->hdmi.output_format != HDMI_COLORSPACE_RGB)
		return true;

	if (conn_state->hdmi.broadcast_rgb == DRM_HDMI_BROADCAST_RGB_FULL)
		return false;

	if (conn_state->hdmi.broadcast_rgb == DRM_HDMI_BROADCAST_RGB_LIMITED)
		return true;

	if (!info->is_hdmi)
		return false;

	return drm_default_rgb_quant_range(mode) == HDMI_QUANTIZATION_RANGE_LIMITED;
}

static bool
sink_supports_format_bpc(const struct drm_connector *connector,
			 const struct drm_display_info *info,
			 const struct drm_display_mode *mode,
			 unsigned int format, unsigned int bpc)
{
	struct drm_device *dev = connector->dev;
	u8 vic = drm_match_cea_mode(mode);

	/*
	 * CTA-861-F, section 5.4 - Color Coding & Quantization states
	 * that the bpc must be 8, 10, 12 or 16 except for the default
	 * 640x480 VIC1 where the value must be 8.
	 *
	 * The definition of default here is ambiguous but the spec
	 * refers to VIC1 being the default timing in several occasions
	 * so our understanding is that for the default timing (ie,
	 * VIC1), the bpc must be 8.
	 */
	if (vic == 1 && bpc != 8) {
		drm_dbg_kms(dev, "VIC1 requires a bpc of 8, got %u\n", bpc);
		return false;
	}

	if (!info->is_hdmi &&
	    (format != HDMI_COLORSPACE_RGB || bpc != 8)) {
		drm_dbg_kms(dev, "DVI Monitors require an RGB output at 8 bpc\n");
		return false;
	}

	if (!(connector->hdmi.supported_formats & BIT(format))) {
		drm_dbg_kms(dev, "%s format unsupported by the connector.\n",
			    drm_hdmi_connector_get_output_format_name(format));
		return false;
	}

	switch (format) {
	case HDMI_COLORSPACE_RGB:
		drm_dbg_kms(dev, "RGB Format, checking the constraints.\n");

		/*
		 * In some cases, like when the EDID readout fails, or
		 * is not an HDMI compliant EDID for some reason, the
		 * color_formats field will be blank and not report any
		 * format supported. In such a case, assume that RGB is
		 * supported so we can keep things going and light up
		 * the display.
		 */
		if (!(info->color_formats & DRM_COLOR_FORMAT_RGB444))
			drm_warn(dev, "HDMI Sink doesn't support RGB, something's wrong.\n");

		if (bpc == 10 && !(info->edid_hdmi_rgb444_dc_modes & DRM_EDID_HDMI_DC_30)) {
			drm_dbg_kms(dev, "10 BPC but sink doesn't support Deep Color 30.\n");
			return false;
		}

		if (bpc == 12 && !(info->edid_hdmi_rgb444_dc_modes & DRM_EDID_HDMI_DC_36)) {
			drm_dbg_kms(dev, "12 BPC but sink doesn't support Deep Color 36.\n");
			return false;
		}

		drm_dbg_kms(dev, "RGB format supported in that configuration.\n");

		return true;

	case HDMI_COLORSPACE_YUV420:
		/* TODO: YUV420 is unsupported at the moment. */
		drm_dbg_kms(dev, "YUV420 format isn't supported yet.\n");
		return false;

	case HDMI_COLORSPACE_YUV422:
		drm_dbg_kms(dev, "YUV422 format, checking the constraints.\n");

		if (!(info->color_formats & DRM_COLOR_FORMAT_YCBCR422)) {
			drm_dbg_kms(dev, "Sink doesn't support YUV422.\n");
			return false;
		}

		if (bpc > 12) {
			drm_dbg_kms(dev, "YUV422 only supports 12 bpc or lower.\n");
			return false;
		}

		/*
		 * HDMI Spec 1.3 - Section 6.5 Pixel Encodings and Color Depth
		 * states that Deep Color is not relevant for YUV422 so we
		 * don't need to check the Deep Color bits in the EDIDs here.
		 */

		drm_dbg_kms(dev, "YUV422 format supported in that configuration.\n");

		return true;

	case HDMI_COLORSPACE_YUV444:
		drm_dbg_kms(dev, "YUV444 format, checking the constraints.\n");

		if (!(info->color_formats & DRM_COLOR_FORMAT_YCBCR444)) {
			drm_dbg_kms(dev, "Sink doesn't support YUV444.\n");
			return false;
		}

		if (bpc == 10 && !(info->edid_hdmi_ycbcr444_dc_modes & DRM_EDID_HDMI_DC_30)) {
			drm_dbg_kms(dev, "10 BPC but sink doesn't support Deep Color 30.\n");
			return false;
		}

		if (bpc == 12 && !(info->edid_hdmi_ycbcr444_dc_modes & DRM_EDID_HDMI_DC_36)) {
			drm_dbg_kms(dev, "12 BPC but sink doesn't support Deep Color 36.\n");
			return false;
		}

		drm_dbg_kms(dev, "YUV444 format supported in that configuration.\n");

		return true;
	}

	drm_dbg_kms(dev, "Unsupported pixel format.\n");
	return false;
}

static enum drm_mode_status
hdmi_clock_valid(const struct drm_connector *connector,
		 const struct drm_display_mode *mode,
		 unsigned long long clock)
{
	const struct drm_connector_hdmi_funcs *funcs = connector->hdmi.funcs;
	const struct drm_display_info *info = &connector->display_info;

	if (info->max_tmds_clock && clock > info->max_tmds_clock * 1000)
		return MODE_CLOCK_HIGH;

	if (funcs && funcs->tmds_char_rate_valid) {
		enum drm_mode_status status;

		status = funcs->tmds_char_rate_valid(connector, mode, clock);
		if (status != MODE_OK)
			return status;
	}

	return MODE_OK;
}

static int
hdmi_compute_clock(const struct drm_connector *connector,
		   struct drm_connector_state *conn_state,
		   const struct drm_display_mode *mode,
		   unsigned int bpc, enum hdmi_colorspace fmt)
{
	enum drm_mode_status status;
	unsigned long long clock;

	clock = drm_hdmi_compute_mode_clock(mode, bpc, fmt);
	if (!clock)
		return -EINVAL;

	status = hdmi_clock_valid(connector, mode, clock);
	if (status != MODE_OK)
		return -EINVAL;

	conn_state->hdmi.tmds_char_rate = clock;

	return 0;
}

static bool
hdmi_try_format_bpc(const struct drm_connector *connector,
		    struct drm_connector_state *conn_state,
		    const struct drm_display_mode *mode,
		    unsigned int bpc, enum hdmi_colorspace fmt)
{
	const struct drm_display_info *info = &connector->display_info;
	struct drm_device *dev = connector->dev;
	int ret;

	drm_dbg_kms(dev, "Trying %s output format\n",
		    drm_hdmi_connector_get_output_format_name(fmt));

	if (!sink_supports_format_bpc(connector, info, mode, fmt, bpc)) {
		drm_dbg_kms(dev, "%s output format not supported with %u bpc\n",
			    drm_hdmi_connector_get_output_format_name(fmt),
			    bpc);
		return false;
	}

	ret = hdmi_compute_clock(connector, conn_state, mode, bpc, fmt);
	if (ret) {
		drm_dbg_kms(dev, "Couldn't compute clock for %s output format and %u bpc\n",
			    drm_hdmi_connector_get_output_format_name(fmt),
			    bpc);
		return false;
	}

	drm_dbg_kms(dev, "%s output format supported with %u (TMDS char rate: %llu Hz)\n",
		    drm_hdmi_connector_get_output_format_name(fmt),
		    bpc, conn_state->hdmi.tmds_char_rate);

	return true;
}

static int
hdmi_compute_format(const struct drm_connector *connector,
		    struct drm_connector_state *conn_state,
		    const struct drm_display_mode *mode,
		    unsigned int bpc)
{
	struct drm_device *dev = connector->dev;

	/*
	 * TODO: Add support for YCbCr420 output for HDMI 2.0 capable
	 * devices, for modes that only support YCbCr420.
	 */
	if (hdmi_try_format_bpc(connector, conn_state, mode, bpc, HDMI_COLORSPACE_RGB)) {
		conn_state->hdmi.output_format = HDMI_COLORSPACE_RGB;
		return 0;
	}

	drm_dbg_kms(dev, "Failed. No Format Supported for that bpc count.\n");

	return -EINVAL;
}

static int
hdmi_compute_config(const struct drm_connector *connector,
		    struct drm_connector_state *conn_state,
		    const struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	unsigned int max_bpc = clamp_t(unsigned int,
				       conn_state->max_bpc,
				       8, connector->max_bpc);
	unsigned int bpc;
	int ret;

	for (bpc = max_bpc; bpc >= 8; bpc -= 2) {
		drm_dbg_kms(dev, "Trying with a %d bpc output\n", bpc);

		ret = hdmi_compute_format(connector, conn_state, mode, bpc);
		if (ret)
			continue;

		conn_state->hdmi.output_bpc = bpc;

		drm_dbg_kms(dev,
			    "Mode %ux%u @ %uHz: Found configuration: bpc: %u, fmt: %s, clock: %llu\n",
			    mode->hdisplay, mode->vdisplay, drm_mode_vrefresh(mode),
			    conn_state->hdmi.output_bpc,
			    drm_hdmi_connector_get_output_format_name(conn_state->hdmi.output_format),
			    conn_state->hdmi.tmds_char_rate);

		return 0;
	}

	return -EINVAL;
}

static int hdmi_generate_avi_infoframe(const struct drm_connector *connector,
				       struct drm_connector_state *conn_state)
{
	const struct drm_display_mode *mode =
		connector_state_get_mode(conn_state);
	struct drm_connector_hdmi_infoframe *infoframe =
		&conn_state->hdmi.infoframes.avi;
	struct hdmi_avi_infoframe *frame =
		&infoframe->data.avi;
	bool is_limited_range = conn_state->hdmi.is_limited_range;
	enum hdmi_quantization_range rgb_quant_range =
		is_limited_range ? HDMI_QUANTIZATION_RANGE_LIMITED : HDMI_QUANTIZATION_RANGE_FULL;
	int ret;

	ret = drm_hdmi_avi_infoframe_from_display_mode(frame, connector, mode);
	if (ret)
		return ret;

	frame->colorspace = conn_state->hdmi.output_format;

	/*
	 * FIXME: drm_hdmi_avi_infoframe_quant_range() doesn't handle
	 * YUV formats at all at the moment, so if we ever support YUV
	 * formats this needs to be revised.
	 */
	drm_hdmi_avi_infoframe_quant_range(frame, connector, mode, rgb_quant_range);
	drm_hdmi_avi_infoframe_colorimetry(frame, conn_state);
	drm_hdmi_avi_infoframe_bars(frame, conn_state);

	infoframe->set = true;

	return 0;
}

static int hdmi_generate_spd_infoframe(const struct drm_connector *connector,
				       struct drm_connector_state *conn_state)
{
	struct drm_connector_hdmi_infoframe *infoframe =
		&conn_state->hdmi.infoframes.spd;
	struct hdmi_spd_infoframe *frame =
		&infoframe->data.spd;
	int ret;

	ret = hdmi_spd_infoframe_init(frame,
				      connector->hdmi.vendor,
				      connector->hdmi.product);
	if (ret)
		return ret;

	frame->sdi = HDMI_SPD_SDI_PC;

	infoframe->set = true;

	return 0;
}

static int hdmi_generate_hdr_infoframe(const struct drm_connector *connector,
				       struct drm_connector_state *conn_state)
{
	struct drm_connector_hdmi_infoframe *infoframe =
		&conn_state->hdmi.infoframes.hdr_drm;
	struct hdmi_drm_infoframe *frame =
		&infoframe->data.drm;
	int ret;

	if (connector->max_bpc < 10)
		return 0;

	if (!conn_state->hdr_output_metadata)
		return 0;

	ret = drm_hdmi_infoframe_set_hdr_metadata(frame, conn_state);
	if (ret)
		return ret;

	infoframe->set = true;

	return 0;
}

static int hdmi_generate_hdmi_vendor_infoframe(const struct drm_connector *connector,
					       struct drm_connector_state *conn_state)
{
	const struct drm_display_info *info = &connector->display_info;
	const struct drm_display_mode *mode =
		connector_state_get_mode(conn_state);
	struct drm_connector_hdmi_infoframe *infoframe =
		&conn_state->hdmi.infoframes.hdmi;
	struct hdmi_vendor_infoframe *frame =
		&infoframe->data.vendor.hdmi;
	int ret;

	if (!info->has_hdmi_infoframe)
		return 0;

	ret = drm_hdmi_vendor_infoframe_from_display_mode(frame, connector, mode);
	if (ret)
		return ret;

	infoframe->set = true;

	return 0;
}

static int
hdmi_generate_infoframes(const struct drm_connector *connector,
			 struct drm_connector_state *conn_state)
{
	const struct drm_display_info *info = &connector->display_info;
	int ret;

	if (!info->is_hdmi)
		return 0;

	ret = hdmi_generate_avi_infoframe(connector, conn_state);
	if (ret)
		return ret;

	ret = hdmi_generate_spd_infoframe(connector, conn_state);
	if (ret)
		return ret;

	/*
	 * Audio Infoframes will be generated by ALSA, and updated by
	 * drm_atomic_helper_connector_hdmi_update_audio_infoframe().
	 */

	ret = hdmi_generate_hdr_infoframe(connector, conn_state);
	if (ret)
		return ret;

	ret = hdmi_generate_hdmi_vendor_infoframe(connector, conn_state);
	if (ret)
		return ret;

	return 0;
}

/**
 * drm_atomic_helper_connector_hdmi_check() - Helper to check HDMI connector atomic state
 * @connector: DRM Connector
 * @state: the DRM State object
 *
 * Provides a default connector state check handler for HDMI connectors.
 * Checks that a desired connector update is valid, and updates various
 * fields of derived state.
 *
 * RETURNS:
 * Zero on success, or an errno code otherwise.
 */
int drm_atomic_helper_connector_hdmi_check(struct drm_connector *connector,
					   struct drm_atomic_state *state)
{
	struct drm_connector_state *old_conn_state =
		drm_atomic_get_old_connector_state(state, connector);
	struct drm_connector_state *new_conn_state =
		drm_atomic_get_new_connector_state(state, connector);
	const struct drm_display_mode *mode =
		connector_state_get_mode(new_conn_state);
	int ret;

	new_conn_state->hdmi.is_limited_range = hdmi_is_limited_range(connector, new_conn_state);

	ret = hdmi_compute_config(connector, new_conn_state, mode);
	if (ret)
		return ret;

	ret = hdmi_generate_infoframes(connector, new_conn_state);
	if (ret)
		return ret;

	if (old_conn_state->hdmi.broadcast_rgb != new_conn_state->hdmi.broadcast_rgb ||
	    old_conn_state->hdmi.output_bpc != new_conn_state->hdmi.output_bpc ||
	    old_conn_state->hdmi.output_format != new_conn_state->hdmi.output_format) {
		struct drm_crtc *crtc = new_conn_state->crtc;
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		crtc_state->mode_changed = true;
	}

	return 0;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_hdmi_check);

static int clear_device_infoframe(struct drm_connector *connector,
				  enum hdmi_infoframe_type type)
{
	const struct drm_connector_hdmi_funcs *funcs = connector->hdmi.funcs;
	struct drm_device *dev = connector->dev;
	int ret;

	drm_dbg_kms(dev, "Clearing infoframe type 0x%x\n", type);

	if (!funcs || !funcs->clear_infoframe) {
		drm_dbg_kms(dev, "Function not implemented, bailing.\n");
		return 0;
	}

	ret = funcs->clear_infoframe(connector, type);
	if (ret) {
		drm_dbg_kms(dev, "Call failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int clear_infoframe(struct drm_connector *connector,
			   struct drm_connector_hdmi_infoframe *old_frame)
{
	int ret;

	ret = clear_device_infoframe(connector, old_frame->data.any.type);
	if (ret)
		return ret;

	return 0;
}

static int write_device_infoframe(struct drm_connector *connector,
				  union hdmi_infoframe *frame)
{
	const struct drm_connector_hdmi_funcs *funcs = connector->hdmi.funcs;
	struct drm_device *dev = connector->dev;
	u8 buffer[HDMI_INFOFRAME_SIZE(MAX)];
	int ret;
	int len;

	drm_dbg_kms(dev, "Writing infoframe type %x\n", frame->any.type);

	if (!funcs || !funcs->write_infoframe) {
		drm_dbg_kms(dev, "Function not implemented, bailing.\n");
		return -EINVAL;
	}

	len = hdmi_infoframe_pack(frame, buffer, sizeof(buffer));
	if (len < 0)
		return len;

	ret = funcs->write_infoframe(connector, frame->any.type, buffer, len);
	if (ret) {
		drm_dbg_kms(dev, "Call failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int write_infoframe(struct drm_connector *connector,
			   struct drm_connector_hdmi_infoframe *new_frame)
{
	int ret;

	ret = write_device_infoframe(connector, &new_frame->data);
	if (ret)
		return ret;

	return 0;
}

static int write_or_clear_infoframe(struct drm_connector *connector,
				    struct drm_connector_hdmi_infoframe *old_frame,
				    struct drm_connector_hdmi_infoframe *new_frame)
{
	if (new_frame->set)
		return write_infoframe(connector, new_frame);

	if (old_frame->set && !new_frame->set)
		return clear_infoframe(connector, old_frame);

	return 0;
}

/**
 * drm_atomic_helper_connector_hdmi_update_infoframes - Update the Infoframes
 * @connector: A pointer to the HDMI connector
 * @state: The HDMI connector state to generate the infoframe from
 *
 * This function is meant for HDMI connector drivers to write their
 * infoframes. It will typically be used in a
 * @drm_connector_helper_funcs.atomic_enable implementation.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_atomic_helper_connector_hdmi_update_infoframes(struct drm_connector *connector,
						       struct drm_atomic_state *state)
{
	struct drm_connector_state *old_conn_state =
		drm_atomic_get_old_connector_state(state, connector);
	struct drm_connector_state *new_conn_state =
		drm_atomic_get_new_connector_state(state, connector);
	struct drm_display_info *info = &connector->display_info;
	int ret;

	if (!info->is_hdmi)
		return 0;

	mutex_lock(&connector->hdmi.infoframes.lock);

	ret = write_or_clear_infoframe(connector,
				       &old_conn_state->hdmi.infoframes.avi,
				       &new_conn_state->hdmi.infoframes.avi);
	if (ret)
		goto out;

	if (connector->hdmi.infoframes.audio.set) {
		ret = write_infoframe(connector,
				      &connector->hdmi.infoframes.audio);
		if (ret)
			goto out;
	}

	ret = write_or_clear_infoframe(connector,
				       &old_conn_state->hdmi.infoframes.hdr_drm,
				       &new_conn_state->hdmi.infoframes.hdr_drm);
	if (ret)
		goto out;

	ret = write_or_clear_infoframe(connector,
				       &old_conn_state->hdmi.infoframes.spd,
				       &new_conn_state->hdmi.infoframes.spd);
	if (ret)
		goto out;

	if (info->has_hdmi_infoframe) {
		ret = write_or_clear_infoframe(connector,
					       &old_conn_state->hdmi.infoframes.hdmi,
					       &new_conn_state->hdmi.infoframes.hdmi);
		if (ret)
			goto out;
	}

out:
	mutex_unlock(&connector->hdmi.infoframes.lock);
	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_hdmi_update_infoframes);

/**
 * drm_atomic_helper_connector_hdmi_update_audio_infoframe - Update the Audio Infoframe
 * @connector: A pointer to the HDMI connector
 * @frame: A pointer to the audio infoframe to write
 *
 * This function is meant for HDMI connector drivers to update their
 * audio infoframe. It will typically be used in one of the ALSA hooks
 * (most likely prepare).
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int
drm_atomic_helper_connector_hdmi_update_audio_infoframe(struct drm_connector *connector,
							struct hdmi_audio_infoframe *frame)
{
	struct drm_connector_hdmi_infoframe *infoframe =
		&connector->hdmi.infoframes.audio;
	struct drm_display_info *info = &connector->display_info;
	int ret;

	if (!info->is_hdmi)
		return 0;

	mutex_lock(&connector->hdmi.infoframes.lock);

	memcpy(&infoframe->data, frame, sizeof(infoframe->data));
	infoframe->set = true;

	ret = write_infoframe(connector, infoframe);

	mutex_unlock(&connector->hdmi.infoframes.lock);

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_hdmi_update_audio_infoframe);

/**
 * drm_atomic_helper_connector_hdmi_clear_audio_infoframe - Stop sending the Audio Infoframe
 * @connector: A pointer to the HDMI connector
 *
 * This function is meant for HDMI connector drivers to stop sending their
 * audio infoframe. It will typically be used in one of the ALSA hooks
 * (most likely shutdown).
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int
drm_atomic_helper_connector_hdmi_clear_audio_infoframe(struct drm_connector *connector)
{
	struct drm_connector_hdmi_infoframe *infoframe =
		&connector->hdmi.infoframes.audio;
	struct drm_display_info *info = &connector->display_info;
	int ret;

	if (!info->is_hdmi)
		return 0;

	mutex_lock(&connector->hdmi.infoframes.lock);

	infoframe->set = false;

	ret = clear_infoframe(connector, infoframe);

	memset(&infoframe->data, 0, sizeof(infoframe->data));

	mutex_unlock(&connector->hdmi.infoframes.lock);

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_hdmi_clear_audio_infoframe);

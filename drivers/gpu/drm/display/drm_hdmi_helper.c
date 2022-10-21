// SPDX-License-Identifier: MIT

#include <linux/module.h>

#include <drm/display/drm_hdmi_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>
#include <drm/drm_property.h>

static inline bool is_eotf_supported(u8 output_eotf, u8 sink_eotf)
{
	return sink_eotf & BIT(output_eotf);
}

/**
 * drm_hdmi_infoframe_set_hdr_metadata() - fill an HDMI DRM infoframe with
 *                                         HDR metadata from userspace
 * @frame: HDMI DRM infoframe
 * @conn_state: Connector state containing HDR metadata
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_hdmi_infoframe_set_hdr_metadata(struct hdmi_drm_infoframe *frame,
					const struct drm_connector_state *conn_state)
{
	struct drm_connector *connector;
	struct hdr_output_metadata *hdr_metadata;
	int err;

	if (!frame || !conn_state)
		return -EINVAL;

	connector = conn_state->connector;

	if (!conn_state->hdr_output_metadata)
		return -EINVAL;

	hdr_metadata = conn_state->hdr_output_metadata->data;

	if (!hdr_metadata || !connector)
		return -EINVAL;

	/* Sink EOTF is Bit map while infoframe is absolute values */
	if (!is_eotf_supported(hdr_metadata->hdmi_metadata_type1.eotf,
	    connector->hdr_sink_metadata.hdmi_type1.eotf)) {
		DRM_DEBUG_KMS("EOTF Not Supported\n");
		return -EINVAL;
	}

	err = hdmi_drm_infoframe_init(frame);
	if (err < 0)
		return err;

	frame->eotf = hdr_metadata->hdmi_metadata_type1.eotf;
	frame->metadata_type = hdr_metadata->hdmi_metadata_type1.metadata_type;

	BUILD_BUG_ON(sizeof(frame->display_primaries) !=
		     sizeof(hdr_metadata->hdmi_metadata_type1.display_primaries));
	BUILD_BUG_ON(sizeof(frame->white_point) !=
		     sizeof(hdr_metadata->hdmi_metadata_type1.white_point));

	memcpy(&frame->display_primaries,
	       &hdr_metadata->hdmi_metadata_type1.display_primaries,
	       sizeof(frame->display_primaries));

	memcpy(&frame->white_point,
	       &hdr_metadata->hdmi_metadata_type1.white_point,
	       sizeof(frame->white_point));

	frame->max_display_mastering_luminance =
		hdr_metadata->hdmi_metadata_type1.max_display_mastering_luminance;
	frame->min_display_mastering_luminance =
		hdr_metadata->hdmi_metadata_type1.min_display_mastering_luminance;
	frame->max_fall = hdr_metadata->hdmi_metadata_type1.max_fall;
	frame->max_cll = hdr_metadata->hdmi_metadata_type1.max_cll;

	return 0;
}
EXPORT_SYMBOL(drm_hdmi_infoframe_set_hdr_metadata);

/* HDMI Colorspace Spec Definitions */
#define FULL_COLORIMETRY_MASK		0x1FF
#define NORMAL_COLORIMETRY_MASK		0x3
#define EXTENDED_COLORIMETRY_MASK	0x7
#define EXTENDED_ACE_COLORIMETRY_MASK	0xF

#define C(x) ((x) << 0)
#define EC(x) ((x) << 2)
#define ACE(x) ((x) << 5)

#define HDMI_COLORIMETRY_NO_DATA		0x0
#define HDMI_COLORIMETRY_SMPTE_170M_YCC		(C(1) | EC(0) | ACE(0))
#define HDMI_COLORIMETRY_BT709_YCC		(C(2) | EC(0) | ACE(0))
#define HDMI_COLORIMETRY_XVYCC_601		(C(3) | EC(0) | ACE(0))
#define HDMI_COLORIMETRY_XVYCC_709		(C(3) | EC(1) | ACE(0))
#define HDMI_COLORIMETRY_SYCC_601		(C(3) | EC(2) | ACE(0))
#define HDMI_COLORIMETRY_OPYCC_601		(C(3) | EC(3) | ACE(0))
#define HDMI_COLORIMETRY_OPRGB			(C(3) | EC(4) | ACE(0))
#define HDMI_COLORIMETRY_BT2020_CYCC		(C(3) | EC(5) | ACE(0))
#define HDMI_COLORIMETRY_BT2020_RGB		(C(3) | EC(6) | ACE(0))
#define HDMI_COLORIMETRY_BT2020_YCC		(C(3) | EC(6) | ACE(0))
#define HDMI_COLORIMETRY_DCI_P3_RGB_D65		(C(3) | EC(7) | ACE(0))
#define HDMI_COLORIMETRY_DCI_P3_RGB_THEATER	(C(3) | EC(7) | ACE(1))

static const u32 hdmi_colorimetry_val[] = {
	[DRM_MODE_COLORIMETRY_NO_DATA] = HDMI_COLORIMETRY_NO_DATA,
	[DRM_MODE_COLORIMETRY_SMPTE_170M_YCC] = HDMI_COLORIMETRY_SMPTE_170M_YCC,
	[DRM_MODE_COLORIMETRY_BT709_YCC] = HDMI_COLORIMETRY_BT709_YCC,
	[DRM_MODE_COLORIMETRY_XVYCC_601] = HDMI_COLORIMETRY_XVYCC_601,
	[DRM_MODE_COLORIMETRY_XVYCC_709] = HDMI_COLORIMETRY_XVYCC_709,
	[DRM_MODE_COLORIMETRY_SYCC_601] = HDMI_COLORIMETRY_SYCC_601,
	[DRM_MODE_COLORIMETRY_OPYCC_601] = HDMI_COLORIMETRY_OPYCC_601,
	[DRM_MODE_COLORIMETRY_OPRGB] = HDMI_COLORIMETRY_OPRGB,
	[DRM_MODE_COLORIMETRY_BT2020_CYCC] = HDMI_COLORIMETRY_BT2020_CYCC,
	[DRM_MODE_COLORIMETRY_BT2020_RGB] = HDMI_COLORIMETRY_BT2020_RGB,
	[DRM_MODE_COLORIMETRY_BT2020_YCC] = HDMI_COLORIMETRY_BT2020_YCC,
};

#undef C
#undef EC
#undef ACE

/**
 * drm_hdmi_avi_infoframe_colorimetry() - fill the HDMI AVI infoframe
 *                                       colorimetry information
 * @frame: HDMI AVI infoframe
 * @conn_state: connector state
 */
void drm_hdmi_avi_infoframe_colorimetry(struct hdmi_avi_infoframe *frame,
					const struct drm_connector_state *conn_state)
{
	u32 colorimetry_val;
	u32 colorimetry_index = conn_state->colorspace & FULL_COLORIMETRY_MASK;

	if (colorimetry_index >= ARRAY_SIZE(hdmi_colorimetry_val))
		colorimetry_val = HDMI_COLORIMETRY_NO_DATA;
	else
		colorimetry_val = hdmi_colorimetry_val[colorimetry_index];

	frame->colorimetry = colorimetry_val & NORMAL_COLORIMETRY_MASK;
	/*
	 * ToDo: Extend it for ACE formats as well. Modify the infoframe
	 * structure and extend it in drivers/video/hdmi
	 */
	frame->extended_colorimetry = (colorimetry_val >> 2) &
					EXTENDED_COLORIMETRY_MASK;
}
EXPORT_SYMBOL(drm_hdmi_avi_infoframe_colorimetry);

/**
 * drm_hdmi_avi_infoframe_bars() - fill the HDMI AVI infoframe
 *                                 bar information
 * @frame: HDMI AVI infoframe
 * @conn_state: connector state
 */
void drm_hdmi_avi_infoframe_bars(struct hdmi_avi_infoframe *frame,
				 const struct drm_connector_state *conn_state)
{
	frame->right_bar = conn_state->tv.margins.right;
	frame->left_bar = conn_state->tv.margins.left;
	frame->top_bar = conn_state->tv.margins.top;
	frame->bottom_bar = conn_state->tv.margins.bottom;
}
EXPORT_SYMBOL(drm_hdmi_avi_infoframe_bars);

/**
 * drm_hdmi_avi_infoframe_content_type() - fill the HDMI AVI infoframe
 *                                         content type information, based
 *                                         on correspondent DRM property.
 * @frame: HDMI AVI infoframe
 * @conn_state: DRM display connector state
 *
 */
void drm_hdmi_avi_infoframe_content_type(struct hdmi_avi_infoframe *frame,
					 const struct drm_connector_state *conn_state)
{
	switch (conn_state->content_type) {
	case DRM_MODE_CONTENT_TYPE_GRAPHICS:
		frame->content_type = HDMI_CONTENT_TYPE_GRAPHICS;
		break;
	case DRM_MODE_CONTENT_TYPE_CINEMA:
		frame->content_type = HDMI_CONTENT_TYPE_CINEMA;
		break;
	case DRM_MODE_CONTENT_TYPE_GAME:
		frame->content_type = HDMI_CONTENT_TYPE_GAME;
		break;
	case DRM_MODE_CONTENT_TYPE_PHOTO:
		frame->content_type = HDMI_CONTENT_TYPE_PHOTO;
		break;
	default:
		/* Graphics is the default(0) */
		frame->content_type = HDMI_CONTENT_TYPE_GRAPHICS;
	}

	frame->itc = conn_state->content_type != DRM_MODE_CONTENT_TYPE_NO_DATA;
}
EXPORT_SYMBOL(drm_hdmi_avi_infoframe_content_type);

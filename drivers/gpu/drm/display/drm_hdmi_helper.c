// SPDX-License-Identifier: MIT

#include <linux/export.h>
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
			       connector->display_info.hdr_sink_metadata.hdmi_type1.eotf))
		DRM_DEBUG_KMS("Unknown EOTF %d\n", hdr_metadata->hdmi_metadata_type1.eotf);

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

/**
 * drm_hdmi_compute_mode_clock() - Computes the TMDS Character Rate
 * @mode: Display mode to compute the clock for
 * @bpc: Bits per character
 * @fmt: Output Pixel Format used
 *
 * Returns the TMDS Character Rate for a given mode, bpc count and output format.
 *
 * RETURNS:
 * The TMDS Character Rate, in Hertz, or 0 on error.
 */
unsigned long long
drm_hdmi_compute_mode_clock(const struct drm_display_mode *mode,
			    unsigned int bpc, enum hdmi_colorspace fmt)
{
	unsigned long long clock = mode->clock * 1000ULL;
	unsigned int vic = drm_match_cea_mode(mode);

	/*
	 * CTA-861-G Spec, section 5.4 - Color Coding and Quantization
	 * mandates that VIC 1 always uses 8 bpc.
	 */
	if (vic == 1 && bpc != 8)
		return 0;

	if (fmt == HDMI_COLORSPACE_YUV422) {
		/*
		 * HDMI 1.0 Spec, section 6.5 - Pixel Encoding states that
		 * YUV422 sends 24 bits over three channels, with Cb and Cr
		 * components being sent on odd and even pixels, respectively.
		 *
		 * If fewer than 12 bpc are sent, data are left justified.
		 */
		if (bpc > 12)
			return 0;

		/*
		 * HDMI 1.0 Spec, section 6.5 - Pixel Encoding
		 * specifies that YUV422 sends two 12-bits components over
		 * three TMDS channels per pixel clock, which is equivalent to
		 * three 8-bits components over three channels used by RGB as
		 * far as the clock rate goes.
		 */
		bpc = 8;
	}

	/*
	 * HDMI 2.0 Spec, Section 7.1 - YCbCr 4:2:0 Pixel Encoding
	 * specifies that YUV420 encoding is carried at a TMDS Character Rate
	 * equal to half the pixel clock rate.
	 */
	if (fmt == HDMI_COLORSPACE_YUV420)
		clock = clock / 2;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		clock = clock * 2;

	return DIV_ROUND_CLOSEST_ULL(clock * bpc, 8);
}
EXPORT_SYMBOL(drm_hdmi_compute_mode_clock);

struct drm_hdmi_acr_n_cts_entry {
	unsigned int n;
	unsigned int cts;
};

struct drm_hdmi_acr_data {
	unsigned long tmds_clock_khz;
	struct drm_hdmi_acr_n_cts_entry n_cts_32k,
					n_cts_44k1,
					n_cts_48k;
};

static const struct drm_hdmi_acr_data hdmi_acr_n_cts[] = {
	{
		/* "Other" entry */
		.n_cts_32k =  { .n = 4096, },
		.n_cts_44k1 = { .n = 6272, },
		.n_cts_48k =  { .n = 6144, },
	}, {
		.tmds_clock_khz = 25175,
		.n_cts_32k =  { .n = 4576,  .cts = 28125, },
		.n_cts_44k1 = { .n = 7007,  .cts = 31250, },
		.n_cts_48k =  { .n = 6864,  .cts = 28125, },
	}, {
		.tmds_clock_khz = 25200,
		.n_cts_32k =  { .n = 4096,  .cts = 25200, },
		.n_cts_44k1 = { .n = 6272,  .cts = 28000, },
		.n_cts_48k =  { .n = 6144,  .cts = 25200, },
	}, {
		.tmds_clock_khz = 27000,
		.n_cts_32k =  { .n = 4096,  .cts = 27000, },
		.n_cts_44k1 = { .n = 6272,  .cts = 30000, },
		.n_cts_48k =  { .n = 6144,  .cts = 27000, },
	}, {
		.tmds_clock_khz = 27027,
		.n_cts_32k =  { .n = 4096,  .cts = 27027, },
		.n_cts_44k1 = { .n = 6272,  .cts = 30030, },
		.n_cts_48k =  { .n = 6144,  .cts = 27027, },
	}, {
		.tmds_clock_khz = 54000,
		.n_cts_32k =  { .n = 4096,  .cts = 54000, },
		.n_cts_44k1 = { .n = 6272,  .cts = 60000, },
		.n_cts_48k =  { .n = 6144,  .cts = 54000, },
	}, {
		.tmds_clock_khz = 54054,
		.n_cts_32k =  { .n = 4096,  .cts = 54054, },
		.n_cts_44k1 = { .n = 6272,  .cts = 60060, },
		.n_cts_48k =  { .n = 6144,  .cts = 54054, },
	}, {
		.tmds_clock_khz = 74176,
		.n_cts_32k =  { .n = 11648, .cts = 210937, }, /* and 210938 */
		.n_cts_44k1 = { .n = 17836, .cts = 234375, },
		.n_cts_48k =  { .n = 11648, .cts = 140625, },
	}, {
		.tmds_clock_khz = 74250,
		.n_cts_32k =  { .n = 4096,  .cts = 74250, },
		.n_cts_44k1 = { .n = 6272,  .cts = 82500, },
		.n_cts_48k =  { .n = 6144,  .cts = 74250, },
	}, {
		.tmds_clock_khz = 148352,
		.n_cts_32k =  { .n = 11648, .cts = 421875, },
		.n_cts_44k1 = { .n = 8918,  .cts = 234375, },
		.n_cts_48k =  { .n = 5824,  .cts = 140625, },
	}, {
		.tmds_clock_khz = 148500,
		.n_cts_32k =  { .n = 4096,  .cts = 148500, },
		.n_cts_44k1 = { .n = 6272,  .cts = 165000, },
		.n_cts_48k =  { .n = 6144,  .cts = 148500, },
	}, {
		.tmds_clock_khz = 296703,
		.n_cts_32k =  { .n = 5824,  .cts = 421875, },
		.n_cts_44k1 = { .n = 4459,  .cts = 234375, },
		.n_cts_48k =  { .n = 5824,  .cts = 281250, },
	}, {
		.tmds_clock_khz = 297000,
		.n_cts_32k =  { .n = 3072,  .cts = 222750, },
		.n_cts_44k1 = { .n = 4704,  .cts = 247500, },
		.n_cts_48k =  { .n = 5120,  .cts = 247500, },
	}, {
		.tmds_clock_khz = 593407,
		.n_cts_32k =  { .n = 5824,  .cts = 843750, },
		.n_cts_44k1 = { .n = 8918,  .cts = 937500, },
		.n_cts_48k =  { .n = 5824,  .cts = 562500, },
	}, {
		.tmds_clock_khz = 594000,
		.n_cts_32k =  { .n = 3072,  .cts = 445500, },
		.n_cts_44k1 = { .n = 9408,  .cts = 990000, },
		.n_cts_48k =  { .n = 6144,  .cts = 594000, },
	},
};

static int drm_hdmi_acr_find_tmds_entry(unsigned long tmds_clock_khz)
{
	int i;

	/* skip the "other" entry */
	for (i = 1; i < ARRAY_SIZE(hdmi_acr_n_cts); i++) {
		if (hdmi_acr_n_cts[i].tmds_clock_khz == tmds_clock_khz)
			return i;
	}

	return 0;
}

/**
 * drm_hdmi_acr_get_n_cts() - get N and CTS values for Audio Clock Regeneration
 *
 * @tmds_char_rate: TMDS clock (char rate) as used by the HDMI connector
 * @sample_rate: audio sample rate
 * @out_n: a pointer to write the N value
 * @out_cts: a pointer to write the CTS value
 *
 * Get the N and CTS values (either by calculating them or by returning data
 * from the tables. This follows the HDMI 1.4b Section 7.2 "Audio Sample Clock
 * Capture and Regeneration".
 *
 * Note, @sample_rate corresponds to the Fs value, see sections 7.2.4 - 7.2.6
 * on how to select Fs for non-L-PCM formats.
 */
void
drm_hdmi_acr_get_n_cts(unsigned long long tmds_char_rate,
		       unsigned int sample_rate,
		       unsigned int *out_n,
		       unsigned int *out_cts)
{
	/* be a bit more tolerant, especially for the 1.001 entries */
	unsigned long tmds_clock_khz = DIV_ROUND_CLOSEST_ULL(tmds_char_rate, 1000);
	const struct drm_hdmi_acr_n_cts_entry *entry;
	unsigned int n, cts, mult;
	int tmds_idx;

	tmds_idx = drm_hdmi_acr_find_tmds_entry(tmds_clock_khz);

	/*
	 * Don't change the order, 192 kHz is divisible by 48k and 32k, but it
	 * should use 48k entry.
	 */
	if (sample_rate % 48000 == 0) {
		entry = &hdmi_acr_n_cts[tmds_idx].n_cts_48k;
		mult = sample_rate / 48000;
	} else if (sample_rate % 44100 == 0) {
		entry = &hdmi_acr_n_cts[tmds_idx].n_cts_44k1;
		mult = sample_rate / 44100;
	} else if (sample_rate % 32000 == 0) {
		entry = &hdmi_acr_n_cts[tmds_idx].n_cts_32k;
		mult = sample_rate / 32000;
	} else {
		entry = NULL;
	}

	if (entry) {
		n = entry->n * mult;
		cts = entry->cts;
	} else {
		/* Recommended optimal value, HDMI 1.4b, Section 7.2.1 */
		n = 128 * sample_rate / 1000;
		cts = 0;
	}

	if (!cts)
		cts = DIV_ROUND_CLOSEST_ULL(tmds_char_rate * n,
					    128 * sample_rate);

	*out_n = n;
	*out_cts = cts;
}
EXPORT_SYMBOL(drm_hdmi_acr_get_n_cts);

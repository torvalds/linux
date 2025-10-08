// SPDX-License-Identifier: MIT

#include <linux/export.h>

#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>

#include <drm/display/drm_hdmi_audio_helper.h>
#include <drm/display/drm_hdmi_cec_helper.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>

/**
 * DOC: hdmi helpers
 *
 * These functions contain an implementation of the HDMI specification
 * in the form of KMS helpers.
 *
 * It contains TMDS character rate computation, automatic selection of
 * output formats, infoframes generation, etc.
 *
 * Infoframes Compliance
 * ~~~~~~~~~~~~~~~~~~~~~
 *
 * Drivers using the helpers will expose the various infoframes
 * generated according to the HDMI specification in debugfs.
 *
 * Compliance can then be tested using ``edid-decode`` from the ``v4l-utils`` project
 * (https://git.linuxtv.org/v4l-utils.git/). A sample run would look like:
 *
 * .. code-block:: bash
 *
 *	# edid-decode \
 *		-I /sys/kernel/debug/dri/1/HDMI-A-1/infoframes/audio \
 *		-I /sys/kernel/debug/dri/1/HDMI-A-1/infoframes/avi \
 *		-I /sys/kernel/debug/dri/1/HDMI-A-1/infoframes/hdmi \
 *		-I /sys/kernel/debug/dri/1/HDMI-A-1/infoframes/hdr_drm \
 *		-I /sys/kernel/debug/dri/1/HDMI-A-1/infoframes/spd \
 *		/sys/class/drm/card1-HDMI-A-1/edid \
 *		-c
 *
 *	edid-decode (hex):
 *
 *	00 ff ff ff ff ff ff 00 1e 6d f4 5b 1e ef 06 00
 *	07 20 01 03 80 2f 34 78 ea 24 05 af 4f 42 ab 25
 *	0f 50 54 21 08 00 d1 c0 61 40 45 40 01 01 01 01
 *	01 01 01 01 01 01 98 d0 00 40 a1 40 d4 b0 30 20
 *	3a 00 d1 0b 12 00 00 1a 00 00 00 fd 00 3b 3d 1e
 *	b2 31 00 0a 20 20 20 20 20 20 00 00 00 fc 00 4c
 *	47 20 53 44 51 48 44 0a 20 20 20 20 00 00 00 ff
 *	00 32 30 37 4e 54 52 4c 44 43 34 33 30 0a 01 46
 *
 *	02 03 42 72 23 09 07 07 4d 01 03 04 90 12 13 1f
 *	22 5d 5e 5f 60 61 83 01 00 00 6d 03 0c 00 10 00
 *	b8 3c 20 00 60 01 02 03 67 d8 5d c4 01 78 80 03
 *	e3 0f 00 18 e2 00 6a e3 05 c0 00 e6 06 05 01 52
 *	52 51 11 5d 00 a0 a0 40 29 b0 30 20 3a 00 d1 0b
 *	12 00 00 1a 00 00 00 00 00 00 00 00 00 00 00 00
 *	00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *	00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 c3
 *
 *	----------------
 *
 *	Block 0, Base EDID:
 *	  EDID Structure Version & Revision: 1.3
 *	  Vendor & Product Identification:
 *	    Manufacturer: GSM
 *	    Model: 23540
 *	    Serial Number: 454430 (0x0006ef1e)
 *	    Made in: week 7 of 2022
 *	  Basic Display Parameters & Features:
 *	    Digital display
 *	    Maximum image size: 47 cm x 52 cm
 *	    Gamma: 2.20
 *	    DPMS levels: Standby Suspend Off
 *	    RGB color display
 *	    First detailed timing is the preferred timing
 *	  Color Characteristics:
 *	    Red  : 0.6835, 0.3105
 *	    Green: 0.2587, 0.6679
 *	    Blue : 0.1445, 0.0585
 *	    White: 0.3134, 0.3291
 *	  Established Timings I & II:
 *	    DMT 0x04:   640x480    59.940476 Hz   4:3     31.469 kHz     25.175000 MHz
 *	    DMT 0x09:   800x600    60.316541 Hz   4:3     37.879 kHz     40.000000 MHz
 *	    DMT 0x10:  1024x768    60.003840 Hz   4:3     48.363 kHz     65.000000 MHz
 *	  Standard Timings:
 *	    DMT 0x52:  1920x1080   60.000000 Hz  16:9     67.500 kHz    148.500000 MHz
 *	    DMT 0x10:  1024x768    60.003840 Hz   4:3     48.363 kHz     65.000000 MHz
 *	    DMT 0x09:   800x600    60.316541 Hz   4:3     37.879 kHz     40.000000 MHz
 *	  Detailed Timing Descriptors:
 *	    DTD 1:  2560x2880   59.966580 Hz   8:9    185.417 kHz    534.000000 MHz (465 mm x 523 mm)
 *	                 Hfront   48 Hsync  32 Hback  240 Hpol P
 *	                 Vfront    3 Vsync  10 Vback  199 Vpol N
 *	    Display Range Limits:
 *	      Monitor ranges (GTF): 59-61 Hz V, 30-178 kHz H, max dotclock 490 MHz
 *	    Display Product Name: 'LG SDQHD'
 *	    Display Product Serial Number: '207NTRLDC430'
 *	  Extension blocks: 1
 *	Checksum: 0x46
 *
 *	----------------
 *
 *	Block 1, CTA-861 Extension Block:
 *	  Revision: 3
 *	  Basic audio support
 *	  Supports YCbCr 4:4:4
 *	  Supports YCbCr 4:2:2
 *	  Native detailed modes: 2
 *	  Audio Data Block:
 *	    Linear PCM:
 *	      Max channels: 2
 *	      Supported sample rates (kHz): 48 44.1 32
 *	      Supported sample sizes (bits): 24 20 16
 *	  Video Data Block:
 *	    VIC   1:   640x480    59.940476 Hz   4:3     31.469 kHz     25.175000 MHz
 *	    VIC   3:   720x480    59.940060 Hz  16:9     31.469 kHz     27.000000 MHz
 *	    VIC   4:  1280x720    60.000000 Hz  16:9     45.000 kHz     74.250000 MHz
 *	    VIC  16:  1920x1080   60.000000 Hz  16:9     67.500 kHz    148.500000 MHz (native)
 *	    VIC  18:   720x576    50.000000 Hz  16:9     31.250 kHz     27.000000 MHz
 *	    VIC  19:  1280x720    50.000000 Hz  16:9     37.500 kHz     74.250000 MHz
 *	    VIC  31:  1920x1080   50.000000 Hz  16:9     56.250 kHz    148.500000 MHz
 *	    VIC  34:  1920x1080   30.000000 Hz  16:9     33.750 kHz     74.250000 MHz
 *	    VIC  93:  3840x2160   24.000000 Hz  16:9     54.000 kHz    297.000000 MHz
 *	    VIC  94:  3840x2160   25.000000 Hz  16:9     56.250 kHz    297.000000 MHz
 *	    VIC  95:  3840x2160   30.000000 Hz  16:9     67.500 kHz    297.000000 MHz
 *	    VIC  96:  3840x2160   50.000000 Hz  16:9    112.500 kHz    594.000000 MHz
 *	    VIC  97:  3840x2160   60.000000 Hz  16:9    135.000 kHz    594.000000 MHz
 *	  Speaker Allocation Data Block:
 *	    FL/FR - Front Left/Right
 *	  Vendor-Specific Data Block (HDMI), OUI 00-0C-03:
 *	    Source physical address: 1.0.0.0
 *	    Supports_AI
 *	    DC_36bit
 *	    DC_30bit
 *	    DC_Y444
 *	    Maximum TMDS clock: 300 MHz
 *	    Extended HDMI video details:
 *	      HDMI VICs:
 *	        HDMI VIC 1:  3840x2160   30.000000 Hz  16:9     67.500 kHz    297.000000 MHz
 *	        HDMI VIC 2:  3840x2160   25.000000 Hz  16:9     56.250 kHz    297.000000 MHz
 *	        HDMI VIC 3:  3840x2160   24.000000 Hz  16:9     54.000 kHz    297.000000 MHz
 *	  Vendor-Specific Data Block (HDMI Forum), OUI C4-5D-D8:
 *	    Version: 1
 *	    Maximum TMDS Character Rate: 600 MHz
 *	    SCDC Present
 *	    Supports 12-bits/component Deep Color 4:2:0 Pixel Encoding
 *	    Supports 10-bits/component Deep Color 4:2:0 Pixel Encoding
 *	  YCbCr 4:2:0 Capability Map Data Block:
 *	    VIC  96:  3840x2160   50.000000 Hz  16:9    112.500 kHz    594.000000 MHz
 *	    VIC  97:  3840x2160   60.000000 Hz  16:9    135.000 kHz    594.000000 MHz
 *	  Video Capability Data Block:
 *	    YCbCr quantization: No Data
 *	    RGB quantization: Selectable (via AVI Q)
 *	    PT scan behavior: Always Underscanned
 *	    IT scan behavior: Always Underscanned
 *	    CE scan behavior: Always Underscanned
 *	  Colorimetry Data Block:
 *	    BT2020YCC
 *	    BT2020RGB
 *	  HDR Static Metadata Data Block:
 *	    Electro optical transfer functions:
 *	      Traditional gamma - SDR luminance range
 *	      SMPTE ST2084
 *	    Supported static metadata descriptors:
 *	      Static metadata type 1
 *	    Desired content max luminance: 82 (295.365 cd/m^2)
 *	    Desired content max frame-average luminance: 82 (295.365 cd/m^2)
 *	    Desired content min luminance: 81 (0.298 cd/m^2)
 *	  Detailed Timing Descriptors:
 *	    DTD 2:  2560x2880   29.986961 Hz   8:9     87.592 kHz    238.250000 MHz (465 mm x 523 mm)
 *	                 Hfront   48 Hsync  32 Hback   80 Hpol P
 *	                 Vfront    3 Vsync  10 Vback   28 Vpol N
 *	Checksum: 0xc3  Unused space in Extension Block: 43 bytes
 *
 *	----------------
 *
 *	edid-decode 1.29.0-5346
 *	edid-decode SHA: c363e9aa6d70 2025-03-11 11:41:18
 *
 *	Warnings:
 *
 *	Block 1, CTA-861 Extension Block:
 *	  IT Video Formats are overscanned by default, but normally this should be underscanned.
 *	  Video Data Block: VIC 1 and the first DTD are not identical. Is this intended?
 *	  Video Data Block: All VICs are in ascending order, and the first (preferred) VIC <= 4, is that intended?
 *	  Video Capability Data Block: Set Selectable YCbCr Quantization to avoid interop issues.
 *	  Video Capability Data Block: S_PT is equal to S_IT and S_CE, so should be set to 0 instead.
 *	  Colorimetry Data Block: Set the sRGB colorimetry bit to avoid interop issues.
 *	  Display Product Serial Number is set, so the Serial Number in the Base EDID should be 0.
 *	EDID:
 *	  Base EDID: Some timings are out of range of the Monitor Ranges:
 *	    Vertical Freq: 24.000 - 60.317 Hz (Monitor: 59.000 - 61.000 Hz)
 *	    Horizontal Freq: 31.250 - 185.416 kHz (Monitor: 30.000 - 178.000 kHz)
 *	    Maximum Clock: 594.000 MHz (Monitor: 490.000 MHz)
 *
 *	Failures:
 *
 *	Block 1, CTA-861 Extension Block:
 *	  Video Capability Data Block: IT video formats are always underscanned, but bit 7 of Byte 3 of the CTA-861 Extension header is set to overscanned.
 *	EDID:
 *	  CTA-861: Native progressive timings are a mix of several resolutions.
 *
 *	EDID conformity: FAIL
 *
 *	================
 *
 *	InfoFrame of '/sys/kernel/debug/dri/1/HDMI-A-1/infoframes/audio' was empty.
 *
 *	================
 *
 *	edid-decode InfoFrame (hex):
 *
 *	82 02 0d 31 12 28 04 00 00 00 00 00 00 00 00 00
 *	00
 *
 *	----------------
 *
 *	HDMI InfoFrame Checksum: 0x31
 *
 *	AVI InfoFrame
 *	  Version: 2
 *	  Length: 13
 *	  Y: Color Component Sample Format: RGB
 *	  A: Active Format Information Present: Yes
 *	  B: Bar Data Present: Bar Data not present
 *	  S: Scan Information: Composed for an underscanned display
 *	  C: Colorimetry: No Data
 *	  M: Picture Aspect Ratio: 16:9
 *	  R: Active Portion Aspect Ratio: 8
 *	  ITC: IT Content: No Data
 *	  EC: Extended Colorimetry: xvYCC601
 *	  Q: RGB Quantization Range: Limited Range
 *	  SC: Non-Uniform Picture Scaling: No Known non-uniform scaling
 *	  YQ: YCC Quantization Range: Limited Range
 *	  CN: IT Content Type: Graphics
 *	  PR: Pixel Data Repetition Count: 0
 *	  Line Number of End of Top Bar: 0
 *	  Line Number of Start of Bottom Bar: 0
 *	  Pixel Number of End of Left Bar: 0
 *	  Pixel Number of Start of Right Bar: 0
 *
 *	----------------
 *
 *	AVI InfoFrame conformity: PASS
 *
 *	================
 *
 *	edid-decode InfoFrame (hex):
 *
 *	81 01 05 49 03 0c 00 20 01
 *
 *	----------------
 *
 *	HDMI InfoFrame Checksum: 0x49
 *
 *	Vendor-Specific InfoFrame (HDMI), OUI 00-0C-03
 *	  Version: 1
 *	  Length: 5
 *	  HDMI Video Format: HDMI_VIC is present
 *	  HDMI VIC 1:  3840x2160   30.000000 Hz  16:9     67.500 kHz    297.000000 MHz
 *
 *	----------------
 *
 *	Vendor-Specific InfoFrame (HDMI), OUI 00-0C-03 conformity: PASS
 *
 *	================
 *
 *	InfoFrame of '/sys/kernel/debug/dri/1/HDMI-A-1/infoframes/hdr_drm' was empty.
 *
 *	================
 *
 *	edid-decode InfoFrame (hex):
 *
 *	83 01 19 93 42 72 6f 61 64 63 6f 6d 56 69 64 65
 *	6f 63 6f 72 65 00 00 00 00 00 00 00 09
 *
 *	----------------
 *
 *	HDMI InfoFrame Checksum: 0x93
 *
 *	Source Product Description InfoFrame
 *	  Version: 1
 *	  Length: 25
 *	  Vendor Name: 'Broadcom'
 *	  Product Description: 'Videocore'
 *	  Source Information: PC general
 *
 *	----------------
 *
 *	Source Product Description InfoFrame conformity: PASS
 *
 * Testing
 * ~~~~~~~
 *
 * The helpers have unit testing and can be tested using kunit with:
 *
 * .. code-block:: bash
 *
 *	$ ./tools/testing/kunit/kunit.py run \
 *		--kunitconfig=drivers/gpu/drm/tests \
 *		drm_atomic_helper_connector_hdmi_*
 */

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

	if (drm_mode_is_420_only(info, mode) && format != HDMI_COLORSPACE_YUV420) {
		drm_dbg_kms(dev, "Mode can be only supported in YUV420 format.\n");
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
		drm_dbg_kms(dev, "YUV420 format, checking the constraints.\n");

		if (!(info->color_formats & DRM_COLOR_FORMAT_YCBCR420)) {
			drm_dbg_kms(dev, "Sink doesn't support YUV420.\n");
			return false;
		}

		if (!drm_mode_is_420(info, mode)) {
			drm_dbg_kms(dev, "Mode cannot be supported in YUV420 format.\n");
			return false;
		}

		if (bpc == 10 && !(info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_30)) {
			drm_dbg_kms(dev, "10 BPC but sink doesn't support Deep Color 30.\n");
			return false;
		}

		if (bpc == 12 && !(info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_36)) {
			drm_dbg_kms(dev, "12 BPC but sink doesn't support Deep Color 36.\n");
			return false;
		}

		if (bpc == 16 && !(info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_48)) {
			drm_dbg_kms(dev, "16 BPC but sink doesn't support Deep Color 48.\n");
			return false;
		}

		drm_dbg_kms(dev, "YUV420 format supported in that configuration.\n");

		return true;

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

	drm_dbg_kms(dev, "Trying %s output format with %u bpc\n",
		    drm_hdmi_connector_get_output_format_name(fmt),
		    bpc);

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

	drm_dbg_kms(dev, "%s output format supported with %u bpc (TMDS char rate: %llu Hz)\n",
		    drm_hdmi_connector_get_output_format_name(fmt),
		    bpc, conn_state->hdmi.tmds_char_rate);

	return true;
}

static int
hdmi_compute_format_bpc(const struct drm_connector *connector,
			struct drm_connector_state *conn_state,
			const struct drm_display_mode *mode,
			unsigned int max_bpc, enum hdmi_colorspace fmt)
{
	struct drm_device *dev = connector->dev;
	unsigned int bpc;
	int ret;

	for (bpc = max_bpc; bpc >= 8; bpc -= 2) {
		ret = hdmi_try_format_bpc(connector, conn_state, mode, bpc, fmt);
		if (!ret)
			continue;

		conn_state->hdmi.output_bpc = bpc;
		conn_state->hdmi.output_format = fmt;

		drm_dbg_kms(dev,
			    "Mode %ux%u @ %uHz: Found configuration: bpc: %u, fmt: %s, clock: %llu\n",
			    mode->hdisplay, mode->vdisplay, drm_mode_vrefresh(mode),
			    conn_state->hdmi.output_bpc,
			    drm_hdmi_connector_get_output_format_name(conn_state->hdmi.output_format),
			    conn_state->hdmi.tmds_char_rate);

		return 0;
	}

	drm_dbg_kms(dev, "Failed. %s output format not supported for any bpc count.\n",
		    drm_hdmi_connector_get_output_format_name(fmt));

	return -EINVAL;
}

static int
hdmi_compute_config(const struct drm_connector *connector,
		    struct drm_connector_state *conn_state,
		    const struct drm_display_mode *mode)
{
	unsigned int max_bpc = clamp_t(unsigned int,
				       conn_state->max_bpc,
				       8, connector->max_bpc);
	int ret;

	ret = hdmi_compute_format_bpc(connector, conn_state, mode, max_bpc,
				      HDMI_COLORSPACE_RGB);
	if (ret) {
		if (connector->ycbcr_420_allowed) {
			ret = hdmi_compute_format_bpc(connector, conn_state,
						      mode, max_bpc,
						      HDMI_COLORSPACE_YUV420);
			if (ret)
				drm_dbg_kms(connector->dev,
					    "YUV420 output format doesn't work.\n");
		} else {
			drm_dbg_kms(connector->dev,
				    "YUV420 output format not allowed for connector.\n");
			ret = -EINVAL;
		}
	}

	return ret;
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

	infoframe->set = false;

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

	infoframe->set = false;

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

	infoframe->set = false;

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

	infoframe->set = false;

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

	if (!new_conn_state->crtc || !new_conn_state->best_encoder)
		return 0;

	ret = hdmi_compute_config(connector, new_conn_state, mode);
	if (ret)
		return ret;

	new_conn_state->hdmi.is_limited_range = hdmi_is_limited_range(connector, new_conn_state);

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

/**
 * drm_hdmi_connector_mode_valid() - Check if mode is valid for HDMI connector
 * @connector: DRM connector to validate the mode
 * @mode: Display mode to validate
 *
 * Generic .mode_valid implementation for HDMI connectors.
 */
enum drm_mode_status
drm_hdmi_connector_mode_valid(struct drm_connector *connector,
			      const struct drm_display_mode *mode)
{
	unsigned long long clock;

	clock = drm_hdmi_compute_mode_clock(mode, 8, HDMI_COLORSPACE_RGB);
	if (!clock)
		return MODE_ERROR;

	return hdmi_clock_valid(connector, mode, clock);
}
EXPORT_SYMBOL(drm_hdmi_connector_mode_valid);

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

static void
drm_atomic_helper_connector_hdmi_update(struct drm_connector *connector,
					enum drm_connector_status status)
{
	const struct drm_edid *drm_edid;

	if (status == connector_status_disconnected) {
		// TODO: also handle scramber, HDMI sink disconnected.
		drm_connector_hdmi_audio_plugged_notify(connector, false);
		drm_edid_connector_update(connector, NULL);
		drm_connector_cec_phys_addr_invalidate(connector);
		return;
	}

	if (connector->hdmi.funcs->read_edid)
		drm_edid = connector->hdmi.funcs->read_edid(connector);
	else
		drm_edid = drm_edid_read(connector);

	drm_edid_connector_update(connector, drm_edid);

	drm_edid_free(drm_edid);

	if (status == connector_status_connected) {
		// TODO: also handle scramber, HDMI sink is now connected.
		drm_connector_hdmi_audio_plugged_notify(connector, true);
		drm_connector_cec_phys_addr_set(connector);
	}
}

/**
 * drm_atomic_helper_connector_hdmi_hotplug - Handle the hotplug event for the HDMI connector
 * @connector: A pointer to the HDMI connector
 * @status: Connection status
 *
 * This function should be called as a part of the .detect() / .detect_ctx()
 * callbacks for all status changes.
 */
void drm_atomic_helper_connector_hdmi_hotplug(struct drm_connector *connector,
					      enum drm_connector_status status)
{
	drm_atomic_helper_connector_hdmi_update(connector, status);
}
EXPORT_SYMBOL(drm_atomic_helper_connector_hdmi_hotplug);

/**
 * drm_atomic_helper_connector_hdmi_force - HDMI Connector implementation of the force callback
 * @connector: A pointer to the HDMI connector
 *
 * This function implements the .force() callback for the HDMI connectors. It
 * can either be used directly as the callback or should be called from within
 * the .force() callback implementation to maintain the HDMI-specific
 * connector's data.
 */
void drm_atomic_helper_connector_hdmi_force(struct drm_connector *connector)
{
	drm_atomic_helper_connector_hdmi_update(connector, connector->status);
}
EXPORT_SYMBOL(drm_atomic_helper_connector_hdmi_force);

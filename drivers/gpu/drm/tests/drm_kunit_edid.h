/* SPDX-License-Identifier: GPL-2.0 */

#ifndef DRM_KUNIT_EDID_H_
#define DRM_KUNIT_EDID_H_

extern const unsigned char test_edid_dvi_1080p[128];
extern const unsigned char test_edid_hdmi_1080p_rgb_max_100mhz[256];
extern const unsigned char test_edid_hdmi_1080p_rgb_max_200mhz[256];
extern const unsigned char test_edid_hdmi_1080p_rgb_max_200mhz_hdr[256];
extern const unsigned char test_edid_hdmi_1080p_rgb_max_340mhz[256];
extern const unsigned char test_edid_hdmi_1080p_rgb_yuv_dc_max_200mhz[256];
extern const unsigned char test_edid_hdmi_1080p_rgb_yuv_dc_max_340mhz[256];
extern const unsigned char test_edid_hdmi_1080p_rgb_yuv_4k_yuv420_dc_max_200mhz[256];
extern const unsigned char test_edid_hdmi_4k_rgb_yuv420_dc_max_340mhz[256];

#endif // DRM_KUNIT_EDID_H_

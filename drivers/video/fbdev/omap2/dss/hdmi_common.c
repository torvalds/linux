
/*
 * Logic for the below structure :
 * user enters the CEA or VESA timings by specifying the HDMI/DVI code.
 * There is a correspondence between CEA/VESA timing and code, please
 * refer to section 6.3 in HDMI 1.3 specification for timing code.
 *
 * In the below structure, cea_vesa_timings corresponds to all OMAP4
 * supported CEA and VESA timing values.code_cea corresponds to the CEA
 * code, It is used to get the timing from cea_vesa_timing array.Similarly
 * with code_vesa. Code_index is used for back mapping, that is once EDID
 * is read from the TV, EDID is parsed to find the timing values and then
 * map it to corresponding CEA or VESA index.
 */

#define DSS_SUBSYS_NAME "HDMI"

#include <linux/kernel.h>
#include <linux/err.h>
#include <video/omapdss.h>

#include "hdmi.h"

static const struct hdmi_config cea_timings[] = {
	{
		{ 640, 480, 25200000, 96, 16, 48, 2, 10, 33,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 1, HDMI_HDMI },
	},
	{
		{ 720, 480, 27027000, 62, 16, 60, 6, 9, 30,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 2, HDMI_HDMI },
	},
	{
		{ 1280, 720, 74250000, 40, 110, 220, 5, 5, 20,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 4, HDMI_HDMI },
	},
	{
		{ 1920, 540, 74250000, 44, 88, 148, 5, 2, 15,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			true, },
		{ 5, HDMI_HDMI },
	},
	{
		{ 1440, 240, 27027000, 124, 38, 114, 3, 4, 15,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			true, },
		{ 6, HDMI_HDMI },
	},
	{
		{ 1920, 1080, 148500000, 44, 88, 148, 5, 4, 36,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 16, HDMI_HDMI },
	},
	{
		{ 720, 576, 27000000, 64, 12, 68, 5, 5, 39,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 17, HDMI_HDMI },
	},
	{
		{ 1280, 720, 74250000, 40, 440, 220, 5, 5, 20,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 19, HDMI_HDMI },
	},
	{
		{ 1920, 540, 74250000, 44, 528, 148, 5, 2, 15,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			true, },
		{ 20, HDMI_HDMI },
	},
	{
		{ 1440, 288, 27000000, 126, 24, 138, 3, 2, 19,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			true, },
		{ 21, HDMI_HDMI },
	},
	{
		{ 1440, 576, 54000000, 128, 24, 136, 5, 5, 39,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 29, HDMI_HDMI },
	},
	{
		{ 1920, 1080, 148500000, 44, 528, 148, 5, 4, 36,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 31, HDMI_HDMI },
	},
	{
		{ 1920, 1080, 74250000, 44, 638, 148, 5, 4, 36,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 32, HDMI_HDMI },
	},
	{
		{ 2880, 480, 108108000, 248, 64, 240, 6, 9, 30,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 35, HDMI_HDMI },
	},
	{
		{ 2880, 576, 108000000, 256, 48, 272, 5, 5, 39,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 37, HDMI_HDMI },
	},
};

static const struct hdmi_config vesa_timings[] = {
/* VESA From Here */
	{
		{ 640, 480, 25175000, 96, 16, 48, 2, 11, 31,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 4, HDMI_DVI },
	},
	{
		{ 800, 600, 40000000, 128, 40, 88, 4, 1, 23,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 9, HDMI_DVI },
	},
	{
		{ 848, 480, 33750000, 112, 16, 112, 8, 6, 23,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0xE, HDMI_DVI },
	},
	{
		{ 1280, 768, 79500000, 128, 64, 192, 7, 3, 20,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x17, HDMI_DVI },
	},
	{
		{ 1280, 800, 83500000, 128, 72, 200, 6, 3, 22,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x1C, HDMI_DVI },
	},
	{
		{ 1360, 768, 85500000, 112, 64, 256, 6, 3, 18,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x27, HDMI_DVI },
	},
	{
		{ 1280, 960, 108000000, 112, 96, 312, 3, 1, 36,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x20, HDMI_DVI },
	},
	{
		{ 1280, 1024, 108000000, 112, 48, 248, 3, 1, 38,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x23, HDMI_DVI },
	},
	{
		{ 1024, 768, 65000000, 136, 24, 160, 6, 3, 29,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x10, HDMI_DVI },
	},
	{
		{ 1400, 1050, 121750000, 144, 88, 232, 4, 3, 32,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x2A, HDMI_DVI },
	},
	{
		{ 1440, 900, 106500000, 152, 80, 232, 6, 3, 25,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x2F, HDMI_DVI },
	},
	{
		{ 1680, 1050, 146250000, 176 , 104, 280, 6, 3, 30,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_LOW,
			false, },
		{ 0x3A, HDMI_DVI },
	},
	{
		{ 1366, 768, 85500000, 143, 70, 213, 3, 3, 24,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x51, HDMI_DVI },
	},
	{
		{ 1920, 1080, 148500000, 44, 148, 80, 5, 4, 36,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x52, HDMI_DVI },
	},
	{
		{ 1280, 768, 68250000, 32, 48, 80, 7, 3, 12,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x16, HDMI_DVI },
	},
	{
		{ 1400, 1050, 101000000, 32, 48, 80, 4, 3, 23,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x29, HDMI_DVI },
	},
	{
		{ 1680, 1050, 119000000, 32, 48, 80, 6, 3, 21,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x39, HDMI_DVI },
	},
	{
		{ 1280, 800, 79500000, 32, 48, 80, 6, 3, 14,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x1B, HDMI_DVI },
	},
	{
		{ 1280, 720, 74250000, 40, 110, 220, 5, 5, 20,
			OMAPDSS_SIG_ACTIVE_HIGH, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x55, HDMI_DVI },
	},
	{
		{ 1920, 1200, 154000000, 32, 48, 80, 6, 3, 26,
			OMAPDSS_SIG_ACTIVE_LOW, OMAPDSS_SIG_ACTIVE_HIGH,
			false, },
		{ 0x44, HDMI_DVI },
	},
};

const struct hdmi_config *hdmi_default_timing(void)
{
	return &vesa_timings[0];
}

static const struct hdmi_config *hdmi_find_timing(int code,
			const struct hdmi_config *timings_arr, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (timings_arr[i].cm.code == code)
			return &timings_arr[i];
	}

	return NULL;
}

const struct hdmi_config *hdmi_get_timings(int mode, int code)
{
	const struct hdmi_config *arr;
	int len;

	if (mode == HDMI_DVI) {
		arr = vesa_timings;
		len = ARRAY_SIZE(vesa_timings);
	} else {
		arr = cea_timings;
		len = ARRAY_SIZE(cea_timings);
	}

	return hdmi_find_timing(code, arr, len);
}

static bool hdmi_timings_compare(struct omap_video_timings *timing1,
			const struct omap_video_timings *timing2)
{
	int timing1_vsync, timing1_hsync, timing2_vsync, timing2_hsync;

	if ((DIV_ROUND_CLOSEST(timing2->pixelclock, 1000000) ==
			DIV_ROUND_CLOSEST(timing1->pixelclock, 1000000)) &&
		(timing2->x_res == timing1->x_res) &&
		(timing2->y_res == timing1->y_res)) {

		timing2_hsync = timing2->hfp + timing2->hsw + timing2->hbp;
		timing1_hsync = timing1->hfp + timing1->hsw + timing1->hbp;
		timing2_vsync = timing2->vfp + timing2->vsw + timing2->vbp;
		timing1_vsync = timing1->vfp + timing1->vsw + timing1->vbp;

		DSSDBG("timing1_hsync = %d timing1_vsync = %d"\
			"timing2_hsync = %d timing2_vsync = %d\n",
			timing1_hsync, timing1_vsync,
			timing2_hsync, timing2_vsync);

		if ((timing1_hsync == timing2_hsync) &&
			(timing1_vsync == timing2_vsync)) {
			return true;
		}
	}
	return false;
}

struct hdmi_cm hdmi_get_code(struct omap_video_timings *timing)
{
	int i;
	struct hdmi_cm cm = {-1};
	DSSDBG("hdmi_get_code\n");

	for (i = 0; i < ARRAY_SIZE(cea_timings); i++) {
		if (hdmi_timings_compare(timing, &cea_timings[i].timings)) {
			cm = cea_timings[i].cm;
			goto end;
		}
	}
	for (i = 0; i < ARRAY_SIZE(vesa_timings); i++) {
		if (hdmi_timings_compare(timing, &vesa_timings[i].timings)) {
			cm = vesa_timings[i].cm;
			goto end;
		}
	}

end:
	return cm;
}

#if defined(CONFIG_OMAP4_DSS_HDMI_AUDIO)
int hdmi_compute_acr(u32 pclk, u32 sample_freq, u32 *n, u32 *cts)
{
	u32 deep_color;
	bool deep_color_correct = false;

	if (n == NULL || cts == NULL)
		return -EINVAL;

	/* TODO: When implemented, query deep color mode here. */
	deep_color = 100;

	/*
	 * When using deep color, the default N value (as in the HDMI
	 * specification) yields to an non-integer CTS. Hence, we
	 * modify it while keeping the restrictions described in
	 * section 7.2.1 of the HDMI 1.4a specification.
	 */
	switch (sample_freq) {
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		if (deep_color == 125)
			if (pclk == 27027000 || pclk == 74250000)
				deep_color_correct = true;
		if (deep_color == 150)
			if (pclk == 27027000)
				deep_color_correct = true;
		break;
	case 44100:
	case 88200:
	case 176400:
		if (deep_color == 125)
			if (pclk == 27027000)
				deep_color_correct = true;
		break;
	default:
		return -EINVAL;
	}

	if (deep_color_correct) {
		switch (sample_freq) {
		case 32000:
			*n = 8192;
			break;
		case 44100:
			*n = 12544;
			break;
		case 48000:
			*n = 8192;
			break;
		case 88200:
			*n = 25088;
			break;
		case 96000:
			*n = 16384;
			break;
		case 176400:
			*n = 50176;
			break;
		case 192000:
			*n = 32768;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (sample_freq) {
		case 32000:
			*n = 4096;
			break;
		case 44100:
			*n = 6272;
			break;
		case 48000:
			*n = 6144;
			break;
		case 88200:
			*n = 12544;
			break;
		case 96000:
			*n = 12288;
			break;
		case 176400:
			*n = 25088;
			break;
		case 192000:
			*n = 24576;
			break;
		default:
			return -EINVAL;
		}
	}
	/* Calculate CTS. See HDMI 1.3a or 1.4a specifications */
	*cts = (pclk/1000) * (*n / 128) * deep_color / (sample_freq / 10);

	return 0;
}
#endif

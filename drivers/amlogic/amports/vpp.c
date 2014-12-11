/*
 * AMLOGIC Audio/Video streaming port driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/amlogic/vout/vinfo.h>
#include <mach/am_regs.h>
#include "amports_config.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#include <mach/vpu.h>
#endif

#include <linux/amlogic/amports/vframe.h>
#include "video.h"
#include "vpp.h"

#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/video_prot.h>

#ifdef CONFIG_AM_DEINTERLACE
#include "deinterlace.h"
#endif

#include "videolog.h"
//#define CONFIG_VIDEO_LOG
#ifdef CONFIG_VIDEO_LOG
#define AMLOG
#endif
#include <linux/amlogic/amlog.h>

/* vpp filter coefficients */
#define COEF_BICUBIC         0
#define COEF_3POINT_TRIANGLE 1
#define COEF_4POINT_TRIANGLE 2
#define COEF_BILINEAR        3
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define COEF_2POINT_BILINEAR 4
#endif

#define MAX_NONLINEAR_FACTOR    0x40

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define VPP_SPEED_FACTOR 0x110ULL
#endif

const u32 vpp_filter_coefs_bicubic_sharp[] = {
    3,
    33 | 0x8000,
    //    0x01f80090, 0x01f80100, 0xff7f0200, 0xfe7f0300,
    0x01fa008c, 0x01fa0100, 0xff7f0200, 0xfe7f0300,
    0xfd7e0500, 0xfc7e0600, 0xfb7d0800, 0xfb7c0900,
    0xfa7b0b00, 0xfa7a0dff, 0xf9790fff, 0xf97711ff,
    0xf87613ff, 0xf87416fe, 0xf87218fe, 0xf8701afe,
    0xf76f1dfd, 0xf76d1ffd, 0xf76b21fd, 0xf76824fd,
    0xf76627fc, 0xf76429fc, 0xf7612cfc, 0xf75f2ffb,
    0xf75d31fb, 0xf75a34fb, 0xf75837fa, 0xf7553afa,
    0xf8523cfa, 0xf8503ff9, 0xf84d42f9, 0xf84a45f9,
    0xf84848f8
};

const u32 vpp_filter_coefs_bicubic[] = {
    4,
    33,
    0x00800000, 0x007f0100, 0xff7f0200, 0xfe7f0300,
    0xfd7e0500, 0xfc7e0600, 0xfb7d0800, 0xfb7c0900,
    0xfa7b0b00, 0xfa7a0dff, 0xf9790fff, 0xf97711ff,
    0xf87613ff, 0xf87416fe, 0xf87218fe, 0xf8701afe,
    0xf76f1dfd, 0xf76d1ffd, 0xf76b21fd, 0xf76824fd,
    0xf76627fc, 0xf76429fc, 0xf7612cfc, 0xf75f2ffb,
    0xf75d31fb, 0xf75a34fb, 0xf75837fa, 0xf7553afa,
    0xf8523cfa, 0xf8503ff9, 0xf84d42f9, 0xf84a45f9,
    0xf84848f8
};

const u32 vpp_filter_coefs_bilinear[] = {
    4,
    33,
    0x00800000, 0x007e0200, 0x007c0400, 0x007a0600,
    0x00780800, 0x00760a00, 0x00740c00, 0x00720e00,
    0x00701000, 0x006e1200, 0x006c1400, 0x006a1600,
    0x00681800, 0x00661a00, 0x00641c00, 0x00621e00,
    0x00602000, 0x005e2200, 0x005c2400, 0x005a2600,
    0x00582800, 0x00562a00, 0x00542c00, 0x00522e00,
    0x00503000, 0x004e3200, 0x004c3400, 0x004a3600,
    0x00483800, 0x00463a00, 0x00443c00, 0x00423e00,
    0x00404000
};

const u32 vpp_filter_coefs_3point_triangle[] = {
    3,
    33,
    0x40400000, 0x3f400100, 0x3d410200, 0x3c410300,
    0x3a420400, 0x39420500, 0x37430600, 0x36430700,
    0x35430800, 0x33450800, 0x32450900, 0x31450a00,
    0x30450b00, 0x2e460c00, 0x2d460d00, 0x2c470d00,
    0x2b470e00, 0x29480f00, 0x28481000, 0x27481100,
    0x26491100, 0x25491200, 0x24491300, 0x234a1300,
    0x224a1400, 0x214a1500, 0x204a1600, 0x1f4b1600,
    0x1e4b1700, 0x1d4b1800, 0x1c4c1800, 0x1b4c1900,
    0x1a4c1a00
};

const u32 vpp_filter_coefs_4point_triangle[] = {
    4,
    33,
    0x20402000, 0x20402000, 0x1f3f2101, 0x1f3f2101,
    0x1e3e2202, 0x1e3e2202, 0x1d3d2303, 0x1d3d2303,
    0x1c3c2404, 0x1c3c2404, 0x1b3b2505, 0x1b3b2505,
    0x1a3a2606, 0x1a3a2606, 0x19392707, 0x19392707,
    0x18382808, 0x18382808, 0x17372909, 0x17372909,
    0x16362a0a, 0x16362a0a, 0x15352b0b, 0x15352b0b,
    0x14342c0c, 0x14342c0c, 0x13332d0d, 0x13332d0d,
    0x12322e0e, 0x12322e0e, 0x11312f0f, 0x11312f0f,
    0x10303010
};

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
const u32 vpp_filter_coefs_2point_binilear[] = {
    2,
    33,
    0x80000000,	0x7e020000, 0x7c040000,	0x7a060000,
    0x78080000,	0x760a0000, 0x740c0000,	0x720e0000,
    0x70100000,	0x6e120000, 0x6c140000,	0x6a160000,
    0x68180000,	0x661a0000, 0x641c0000,	0x621e0000,
    0x60200000,	0x5e220000, 0x5c240000,	0x5a260000,
    0x58280000,	0x562a0000, 0x542c0000,	0x522e0000,
    0x50300000,	0x4e320000, 0x4c340000,	0x4a360000,
    0x48380000,	0x463a0000, 0x443c0000,	0x423e0000,
    0x40400000
};
#endif

static const u32 *filter_table[] = {
    vpp_filter_coefs_bicubic,
    vpp_filter_coefs_3point_triangle,
    vpp_filter_coefs_4point_triangle,
    vpp_filter_coefs_bilinear,
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    vpp_filter_coefs_2point_binilear
#endif
};

static u32 vpp_wide_mode;
static u32 vpp_zoom_ratio = 100;
static s32 vpp_zoom_center_x, vpp_zoom_center_y;
static u32 nonlinear_factor = MAX_NONLINEAR_FACTOR / 2;
static u32 osd_layer_preblend=0;
static s32 video_layer_top, video_layer_left, video_layer_width, video_layer_height;
static u32 video_source_crop_top, video_source_crop_left, video_source_crop_bottom, video_source_crop_right;
static s32 video_layer_global_offset_x, video_layer_global_offset_y;
static s32 osd_layer_top,osd_layer_left,osd_layer_width,osd_layer_height;
static u32 video_speed_check_width=1800, video_speed_check_height=1400;


#ifdef TV_3D_FUNCTION_OPEN
static bool vpp_3d_scale=0;
static int force_filter_mode=1;

MODULE_PARM_DESC(force_filter_mode, "\n force_filter_mode  \n");
module_param(force_filter_mode, int, 0664);

#endif

#if 0
#define DECL_PARM(name)\
static int name;\
MODULE_PARM_DESC(name, "\n "#name"  \n");\
module_param(name, int, 0664);

DECL_PARM(debug_wide_mode)
DECL_PARM(debug_video_left)
DECL_PARM(debug_video_top)
DECL_PARM(debug_video_width)
DECL_PARM(debug_video_height)
DECL_PARM(debug_ratio_x)
DECL_PARM(debug_ratio_y)
#endif

#define ZOOM_BITS       18
#define PHASE_BITS      8

/*
 *   when ratio for Y is 1:1
 *   Line #   In(P)   In(I)       Out(P)      Out(I)            Out(P)      Out(I)
 *   0        P_Y     IT_Y        P_Y          IT_Y
 *   1                                                          P_Y         IT_Y
 *   2                IB_Y                     IB_Y
 *   3                                                                      IB_Y
 *   4        P_Y     IT_Y        P_Y          IT_Y
 *   5                                                          P_Y         IT_Y
 *   6                IB_Y                     IB_Y
 *   7                                                                      IB_Y
 *   8        P_Y     IT_Y        P_Y          IT_Y
 *   9                                                          P_Y         IT_Y
 *  10                IB_Y                     IB_Y
 *  11                                                                      IB_Y
 *  12        P_Y     IT_Y        P_Y          IT_Y
 *                                                              P_Y         IT_Y
 */

/* The table data sequence here is arranged according to f2v_vphase_type_t enum,
 *  IT2IT, IB2IB, T2IB, IB2IT, P2IT, P2IB, IT2P, IB2P, P2P
 */
static const u8 f2v_420_in_pos[F2V_TYPE_MAX] =
{ 0, 2, 0, 2, 0, 0, 0, 2, 0 };
static const u8 f2v_420_out_pos1[F2V_TYPE_MAX] =
{ 0, 2, 2, 0, 0, 2, 0, 0, 0 };
static const u8 f2v_420_out_pos2[F2V_TYPE_MAX] =
{ 1, 3, 3, 1, 1, 3, 1, 1, 1 };

static void f2v_get_vertical_phase(u32 zoom_ratio,
                                   u32 phase_adj,
                                   f2v_vphase_t vphase[F2V_TYPE_MAX],
                                   u32 interlace)
{
    f2v_vphase_type_t type;
    s32 offset_in, offset_out;
    s32 phase;
    const u8 *f2v_420_out_pos;

    if ((interlace == 0) && (zoom_ratio > (1 << ZOOM_BITS))) {
        f2v_420_out_pos = f2v_420_out_pos2;
    } else {
        f2v_420_out_pos = f2v_420_out_pos1;
    }

    for (type = F2V_IT2IT; type < F2V_TYPE_MAX; type++) {
        offset_in = f2v_420_in_pos[type] << PHASE_BITS;
        offset_out =
            (f2v_420_out_pos[type] * zoom_ratio) >> (ZOOM_BITS -
                    PHASE_BITS);

        if (offset_in > offset_out) {
            vphase[type].repeat_skip = -1;     /* repeat line */
            vphase[type].phase =
                ((4 << PHASE_BITS) + offset_out - offset_in) >> 2;

        } else {
            vphase[type].repeat_skip = 0;      /* skip line */

            while ((offset_in + (4 << PHASE_BITS)) <= offset_out) {
                vphase[type].repeat_skip++;
                offset_in += 4 << PHASE_BITS;
            }

            vphase[type].phase = (offset_out - offset_in) >> 2;
        }

        phase = vphase[type].phase + phase_adj;

        if (phase > 0x100) {
            vphase[type].repeat_skip++;
        }

        vphase[type].phase = phase & 0xff;

        if (vphase[type].repeat_skip > 5) {
            vphase[type].repeat_skip = 5;
        }
    }
}

/*
 * V-shape non-linear mode
 */
static void
calculate_non_linear_ratio(unsigned middle_ratio,
                           unsigned width_out,
                           vpp_frame_par_t *next_frame_par)
{
    unsigned diff_ratio;
    vppfilter_mode_t *vpp_filter = &next_frame_par->vpp_filter;

    diff_ratio = middle_ratio * nonlinear_factor;
    vpp_filter->vpp_hf_start_phase_step = (middle_ratio << 6) - diff_ratio;
    vpp_filter->vpp_hf_start_phase_slope = diff_ratio * 4 / width_out;
    vpp_filter->vpp_hf_end_phase_slope =
        vpp_filter->vpp_hf_start_phase_slope | 0x1000000;

    return;
}

static int
vpp_process_speed_check(s32 width_in,
                        s32 height_in,
                        s32 height_out,
                        s32 height_screen,
                        vpp_frame_par_t *next_frame_par,
                        const vinfo_t *vinfo)
{
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if ((width_in <= 0) || (height_in <= 0) || (height_out <= 0) || (height_screen <= 0)) {
        return SPEED_CHECK_DONE;
    }

    if (height_in > height_out) {
        if (height_out == 0 || div_u64(VPP_SPEED_FACTOR * width_in * height_in * vinfo->sync_duration_num * height_screen,
                    height_out * vinfo->sync_duration_den * 256) > get_vpu_clk()) {
            return SPEED_CHECK_VSKIP;
        } else {
            return SPEED_CHECK_DONE;
        }
    } else if (next_frame_par->hscale_skip_count== 0) {
        if (div_u64(VPP_SPEED_FACTOR * width_in * vinfo->sync_duration_num * height_screen,
                    vinfo->sync_duration_den * 256) > get_vpu_clk()) {
            return SPEED_CHECK_HSKIP;
        } else {
            return SPEED_CHECK_DONE;
        }
    }

    return SPEED_CHECK_DONE;
#else
    /* return okay if vpp preblend enabled */
    if ((aml_read_reg32(P_VPP_MISC) & VPP_PREBLEND_EN)&&(aml_read_reg32(P_VPP_MISC) & VPP_OSD1_PREBLEND)) {
        return SPEED_CHECK_DONE;
    }

#if (MESON_CPU_TYPE > MESON_CPU_TYPE_MESON6)
    if ((height_out+1) > height_in) {
        return SPEED_CHECK_DONE;
    }
#else
    if (video_speed_check_width * video_speed_check_height * height_out > height_screen * width_in * height_in) {
        return SPEED_CHECK_DONE;
    }
#endif

    amlog_mask(LOG_MASK_VPP, "vpp_process_speed_check failed\n");

    return SPEED_CHECK_VSKIP;
#endif
}

static void
vpp_set_filters2(u32 width_in,
                 u32 height_in,
                 const vinfo_t *vinfo,
                 u32 vpp_flags,
                 vpp_frame_par_t *next_frame_par)
{
    u32 screen_width, screen_height;
    s32 start, end;
    s32 video_top, video_left, temp;
    u32 video_width, video_height;
    u32 ratio_x = 0;
    u32 ratio_y = 0;
    u32 tmp_ratio_y = 0;
    vppfilter_mode_t *filter = &next_frame_par->vpp_filter;
    u32 wide_mode;
    s32 height_shift = 0;
    u32 height_after_ratio;
    u32 aspect_factor;
    s32 ini_vphase;
    u32 w_in = width_in;
    u32 h_in = height_in;
    bool h_crop_enable = false, v_crop_enable = false;
    u32 width_out = vinfo->width;
    u32 height_out = vinfo->height;
    u32 aspect_ratio_out = (vinfo->aspect_ratio_den << 8) / vinfo->aspect_ratio_num;
	bool fill_match = true;
	u32 orig_aspect = 0;
	u32 screen_aspect = 0;

    if (likely(w_in > (video_source_crop_left + video_source_crop_right))) {
        w_in -= video_source_crop_left + video_source_crop_right;
        h_crop_enable = true;
    }

    if (likely(h_in > (video_source_crop_top + video_source_crop_bottom))) {
        h_in -= video_source_crop_top + video_source_crop_bottom;
        v_crop_enable = true;
    }

#ifdef CONFIG_AM_DEINTERLACE
    int deinterlace_mode = get_deinterlace_mode();
#endif

    next_frame_par->vscale_skip_count = 0;
    next_frame_par->hscale_skip_count = 0;

    if (vpp_flags & VPP_FLAG_INTERLACE_IN) {
        next_frame_par->vscale_skip_count++;
    }
    if (vpp_flags & VPP_FLAG_INTERLACE_OUT) {
        height_shift++;
    }

RESTART:

    aspect_factor = (vpp_flags & VPP_FLAG_AR_MASK) >> VPP_FLAG_AR_BITS;
    wide_mode = vpp_flags & VPP_FLAG_WIDEMODE_MASK;

    /* keep 8 bits resolution for aspect conversion */
    if (wide_mode == VIDEO_WIDEOPTION_4_3) {	
        if (vpp_flags & VPP_FLAG_PORTRAIT_MODE)
            aspect_factor = 0x155;
        else
            aspect_factor = 0xc0;
        wide_mode = VIDEO_WIDEOPTION_NORMAL;
    }
    else if (wide_mode == VIDEO_WIDEOPTION_16_9) {
        if (vpp_flags & VPP_FLAG_PORTRAIT_MODE)
            aspect_factor = 0x1c7;
        else
            aspect_factor = 0x90;
        wide_mode = VIDEO_WIDEOPTION_NORMAL;
    }
	else if ((wide_mode >= VIDEO_WIDEOPTION_4_3_IGNORE) && (wide_mode <= VIDEO_WIDEOPTION_4_3_COMBINED)) {
		if (aspect_factor != 0xc0)
			fill_match = false;

		orig_aspect = aspect_factor;
		screen_aspect = 0xc0;
	}
	else if ((wide_mode >= VIDEO_WIDEOPTION_16_9_IGNORE) && (wide_mode <= VIDEO_WIDEOPTION_16_9_COMBINED)) {
		if (aspect_factor != 0x90)
			fill_match = false;

		orig_aspect = aspect_factor;
		screen_aspect = 0x90;
	}

    if ((aspect_factor == 0) || (wide_mode == VIDEO_WIDEOPTION_FULL_STRETCH) || (wide_mode == VIDEO_WIDEOPTION_NONLINEAR)) {
        aspect_factor = 0x100;
    } else {
        aspect_factor = div_u64((unsigned long long)w_in * height_out * (aspect_factor << 8), width_out * h_in * aspect_ratio_out);
    }

    if(osd_layer_preblend)
    aspect_factor=0x100;

    height_after_ratio = (h_in * aspect_factor) >> 8;

    /* if we have ever set a cropped display area for video layer
     * (by checking video_layer_width/video_height), then
     * it will override the input width_out/height_out for
     * ratio calculations, a.k.a we have a window for video content
     */
    if(osd_layer_preblend){
        if ((osd_layer_width == 0) || (osd_layer_height == 0)) {
            video_top = 0;
            video_left = 0;
            video_width = width_out;
            video_height = height_out;

        } else {
             video_top = osd_layer_top;
             video_left = osd_layer_left;
             video_width = osd_layer_width;
             video_height = osd_layer_height;
        }
    } else {
        video_top = video_layer_top;
        video_left = video_layer_left;
        video_width = video_layer_width;
        video_height = video_layer_height;

        if ((video_top == 0) && (video_left == 0) && (video_width <= 1) && (video_height <= 1)) {
            /* special case to do full screen display */
            video_width = width_out;
            video_height = height_out;
        } else {
        	  if ((video_layer_width < 16) && (video_layer_height < 16)) {
                /* sanity check to move video out when the target size is too small */
                video_width = width_out;
                video_height = height_out;
                video_left = width_out * 2;
            }
            video_top += video_layer_global_offset_y;
            video_left+= video_layer_global_offset_x;
        }
    }

	/*aspect ratio match*/
	if ((wide_mode >= VIDEO_WIDEOPTION_4_3_IGNORE) && (wide_mode <= VIDEO_WIDEOPTION_16_9_COMBINED) && orig_aspect) {
		if ((video_height << 8) > (video_width * aspect_ratio_out)) {
			u32 real_video_height = (video_width * aspect_ratio_out) >> 8;

			video_top   += (video_height - real_video_height) >> 1;
			video_height = real_video_height;
		}
		else {
			u32 real_video_width = (video_height << 8) / aspect_ratio_out;

			video_left  += (video_width - real_video_width) >> 1;
			video_width  = real_video_width;
		}

		if (!fill_match) {
			u32 screen_ratio_x, screen_ratio_y;

			screen_ratio_x = 1 << 18;
			screen_ratio_y = (orig_aspect << 18) / screen_aspect;

			switch (wide_mode) {
				case VIDEO_WIDEOPTION_4_3_LETTER_BOX:
				case VIDEO_WIDEOPTION_16_9_LETTER_BOX:
					screen_ratio_x = screen_ratio_y = max(screen_ratio_x, screen_ratio_y);
					break;
				case VIDEO_WIDEOPTION_4_3_PAN_SCAN:
				case VIDEO_WIDEOPTION_16_9_PAN_SCAN:
					screen_ratio_x = screen_ratio_y = min(screen_ratio_x, screen_ratio_y);
					break;
				case VIDEO_WIDEOPTION_4_3_COMBINED:
				case VIDEO_WIDEOPTION_16_9_COMBINED:
					screen_ratio_x = screen_ratio_y = ((screen_ratio_x + screen_ratio_y) >> 1);
					break;
				default:
					break;
			}

			ratio_x = screen_ratio_x * w_in / video_width;
			ratio_y = screen_ratio_y * h_in / orig_aspect * screen_aspect / video_height;
		}
		else {
			screen_width  = video_width * vpp_zoom_ratio / 100;
			screen_height = video_height * vpp_zoom_ratio / 100;
			
			ratio_x = (w_in << 18) / screen_width;
			ratio_y = (h_in << 18) / screen_height;
		}
	}
	else {
		screen_width = video_width * vpp_zoom_ratio / 100;
		screen_height = video_height * vpp_zoom_ratio / 100;

		ratio_x = (w_in << 18) / screen_width;
		if (ratio_x * screen_width < (w_in << 18)) {
			ratio_x++;
		}

		ratio_y = (height_after_ratio << 18) / screen_height;

		if (wide_mode == VIDEO_WIDEOPTION_NORMAL) {
			ratio_x = ratio_y = max(ratio_x, ratio_y);
			ratio_y = (ratio_y << 8) / aspect_factor;
		}
		else if (wide_mode == VIDEO_WIDEOPTION_NORMAL_NOSCALEUP) {
			u32 r1, r2;
			r1 = max(ratio_x, ratio_y);
			r2 = (r1 << 8) / aspect_factor;

			if ((r1 < (1<<18)) || (r2 < (1<<18))) {
				if (r1 < r2) {
					ratio_x = 1 << 18;
					ratio_y = (ratio_x << 8) / aspect_factor;
				} else {
					ratio_y = 1 << 18;
					ratio_x = aspect_factor << 10;
				}
			} else {
				ratio_x = r1;
				ratio_y = r2;
			}
		}
	}

#if 0
	debug_video_left   = video_left;
	debug_video_top    = video_top;
	debug_video_width  = video_width;
	debug_video_height = video_height;
	debug_ratio_x = ratio_x;
	debug_ratio_y = ratio_y;
	debug_wide_mode = wide_mode;
#endif

    /* vertical */
    ini_vphase = vpp_zoom_center_y & 0xff;

    next_frame_par->VPP_pic_in_height_ = h_in / (next_frame_par->vscale_skip_count + 1);

    /* screen position for source */
    start = video_top + video_height / 2 - ((h_in << 17) + (vpp_zoom_center_y << 10)) / ratio_y;
    end   = (h_in << 18) / ratio_y + start - 1;

    /* calculate source vertical clip */
    if (video_top < 0) {
        if (start < 0) {
            temp = (-start * ratio_y) >> 18;
            next_frame_par->VPP_vd_start_lines_ = temp;

        } else {
            next_frame_par->VPP_vd_start_lines_ = 0;
        }

    } else {
        if (start < video_top) {
            temp = ((video_top - start) * ratio_y) >> 18;
            next_frame_par->VPP_vd_start_lines_ = temp;

        } else {
            next_frame_par->VPP_vd_start_lines_ = 0;
        }
    }

    temp = next_frame_par->VPP_vd_start_lines_ + (video_height * ratio_y >> 18);
    next_frame_par->VPP_vd_end_lines_ = (temp <= (h_in - 1)) ? temp : (h_in - 1);

    if (v_crop_enable) {
        next_frame_par->VPP_vd_start_lines_ += video_source_crop_top;
        next_frame_par->VPP_vd_end_lines_ += video_source_crop_top;
    }

    if (vpp_flags & VPP_FLAG_INTERLACE_IN) {
        next_frame_par->VPP_vd_start_lines_ &= ~1;
    }

    /* find overlapped region between
     * [start, end], [0, height_out-1], [video_top, video_top+video_height-1]
     */
    start = max(start, max(0, video_top));
    end   = min(end, min((s32)height_out - 1, (s32)(video_top + video_height - 1)));

    if (start >= end) {
        /* nothing to display */
        next_frame_par->VPP_vsc_startp = 0;

        next_frame_par->VPP_vsc_endp = 0;

    } else {
        next_frame_par->VPP_vsc_startp =
            (vpp_flags & VPP_FLAG_INTERLACE_OUT) ? (start >> 1) : start;

        next_frame_par->VPP_vsc_endp =
            (vpp_flags & VPP_FLAG_INTERLACE_OUT) ? (end >> 1) : end;
    }

    /* set filter co-efficients */
    tmp_ratio_y = ratio_y;
    ratio_y <<= height_shift;
    ratio_y = ratio_y / (next_frame_par->vscale_skip_count + 1);
#ifdef 	TV_3D_FUNCTION_OPEN
    if (((vpp_flags & VPP_FLAG_INTERLACE_OUT)||next_frame_par->vpp_3d_scale) && force_filter_mode)
#else
    if (vpp_flags & VPP_FLAG_INTERLACE_OUT)
#endif
    {
        filter->vpp_vert_coeff = filter_table[COEF_BILINEAR];
    } else {
        filter->vpp_vert_coeff = filter_table[COEF_BICUBIC];
    }

#ifdef CONFIG_AM_DEINTERLACE
    if (deinterlace_mode) {
        filter->vpp_vert_coeff = filter_table[COEF_3POINT_TRIANGLE];
    }
#endif
    filter->vpp_vsc_start_phase_step = ratio_y << 6;

    f2v_get_vertical_phase(ratio_y, ini_vphase,
                           next_frame_par->VPP_vf_ini_phase_,
                           vpp_flags & VPP_FLAG_INTERLACE_OUT);

    /* horizontal */
    filter->vpp_hf_start_phase_slope = 0;
    filter->vpp_hf_end_phase_slope   = 0;
    filter->vpp_hf_start_phase_step  = ratio_x << 6;

    next_frame_par->VPP_hsc_linear_startp = next_frame_par->VPP_hsc_startp;
    next_frame_par->VPP_hsc_linear_endp   = next_frame_par->VPP_hsc_endp;

    filter->vpp_horz_coeff = filter_table[COEF_BICUBIC];

    filter->vpp_hsc_start_phase_step = ratio_x << 6;
    next_frame_par->VPP_hf_ini_phase_ = vpp_zoom_center_x & 0xff;

    if ((ratio_x == (1 << 18)) && (next_frame_par->VPP_hf_ini_phase_ == 0)) {
        filter->vpp_horz_coeff = vpp_filter_coefs_bicubic_sharp;
    } else {
        filter->vpp_horz_coeff = filter_table[COEF_BICUBIC];
    }

    /* screen position for source */
    start = video_left + video_width / 2 - ((w_in << 17) + (vpp_zoom_center_x << 10)) / ratio_x;
    end   = (w_in << 18) / ratio_x + start - 1;
    /* calculate source horizontal clip */
    if (video_left < 0) {
        if (start < 0) {
            temp = (-start * ratio_x) >> 18;
            next_frame_par->VPP_hd_start_lines_ = temp;

        } else {
            next_frame_par->VPP_hd_start_lines_ = 0;
        }

    } else {
        if (start < video_left) {
            temp = ((video_left - start) * ratio_x) >> 18;
            next_frame_par->VPP_hd_start_lines_ = temp;

        } else {
            next_frame_par->VPP_hd_start_lines_ = 0;
        }
    }

    temp = next_frame_par->VPP_hd_start_lines_ + (video_width * ratio_x >> 18);
    next_frame_par->VPP_hd_end_lines_ = (temp <= (w_in - 1)) ? temp : (w_in - 1);

    if (h_crop_enable) {
        next_frame_par->VPP_hd_start_lines_ += video_source_crop_left;
        next_frame_par->VPP_hd_end_lines_ += video_source_crop_left;
    }

    next_frame_par->VPP_line_in_length_ = next_frame_par->VPP_hd_end_lines_ - next_frame_par->VPP_hd_start_lines_ + 1;
    /* find overlapped region between
     * [start, end], [0, width_out-1], [video_left, video_left+video_width-1]
     */
    start = max(start, max(0, video_left));
    end   = min(end, min((s32)width_out - 1, (s32)(video_left + video_width - 1)));

    if (start >= end) {
        /* nothing to display */
        next_frame_par->VPP_hsc_startp = 0;

        next_frame_par->VPP_hsc_endp = 0;

    } else {
        next_frame_par->VPP_hsc_startp = start;

        next_frame_par->VPP_hsc_endp = end;
    }

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (next_frame_par->VPP_line_in_length_ >= 2048) {
        filter->vpp_vert_coeff = filter_table[COEF_2POINT_BILINEAR];
    }
#endif

    if ((wide_mode == VIDEO_WIDEOPTION_NONLINEAR) && (end > start)) {
        calculate_non_linear_ratio(ratio_x, end - start, next_frame_par);

        next_frame_par->VPP_hsc_linear_startp = next_frame_par->VPP_hsc_linear_endp = (start + end) / 2;
    }

    /* check the painful bandwidth limitation and see
     * if we need skip half resolution on source side for progressive
     * frames.
     */
    if ((next_frame_par->vscale_skip_count < 4)&&(!(vpp_flags & VPP_FLAG_VSCALE_DISABLE))) {
        int skip = vpp_process_speed_check(
                       (next_frame_par->VPP_hd_end_lines_ - next_frame_par->VPP_hd_start_lines_ + 1) / (next_frame_par->hscale_skip_count + 1),
                       (next_frame_par->VPP_vd_end_lines_ - next_frame_par->VPP_vd_start_lines_ + 1) / (next_frame_par->vscale_skip_count + 1),
                       next_frame_par->VPP_vsc_endp - next_frame_par->VPP_vsc_startp,
                       height_out >> ((vpp_flags & VPP_FLAG_INTERLACE_OUT) ? 1 : 0),
                       next_frame_par,
                       vinfo);

        if (skip == SPEED_CHECK_VSKIP) {
            if (vpp_flags & VPP_FLAG_INTERLACE_IN) {
                next_frame_par->vscale_skip_count += 2;
            } else {
#ifdef TV_3D_FUNCTION_OPEN
            if(next_frame_par->vpp_3d_mode == VPP_3D_MODE_LA)
		next_frame_par->vscale_skip_count += 2;
	    else
#endif
                next_frame_par->vscale_skip_count++;
            }
            goto RESTART;

        } else if (skip == SPEED_CHECK_HSKIP) {
            next_frame_par->hscale_skip_count = 1;
        }
    }

    filter->vpp_hsc_start_phase_step = ratio_x << 6;

    if (next_frame_par->hscale_skip_count) {
        filter->vpp_hf_start_phase_step >>= 1;
        next_frame_par->VPP_line_in_length_ >>= 1;
    }

    next_frame_par->VPP_hf_ini_phase_ = vpp_zoom_center_x & 0xff;
#if HAS_VPU_PROT
    if (get_prot_status()){
        s32 tmp_height = (((s32)next_frame_par->VPP_vd_end_lines_ + 1) << 18) / tmp_ratio_y;
        s32 tmp_top = 0;
        s32 tmp_bottom = 0;

        //printk("height_out %d video_height %d\n", height_out, video_height);
        //printk("vf1 %d %d %d %d vs %d %d\n", next_frame_par->VPP_hd_start_lines_, next_frame_par->VPP_hd_end_lines_,
        //        next_frame_par->VPP_vd_start_lines_, next_frame_par->VPP_vd_end_lines_,
        //        next_frame_par->hscale_skip_count, next_frame_par->vscale_skip_count);
        if ((s32)video_height > tmp_height) {
            tmp_top = (s32)video_top + (((s32)video_height - tmp_height) >> 1);
        } else {
            tmp_top = (s32)video_top;
        }
        tmp_bottom = tmp_top + (((s32)next_frame_par->VPP_vd_end_lines_ + 1) << 18) / (s32)tmp_ratio_y;
        if(tmp_bottom > (s32)height_out && tmp_top < (s32)height_out) {
            s32 tmp_end = (s32)next_frame_par->VPP_vd_end_lines_ - ((tmp_bottom - (s32)height_out) * (s32)tmp_ratio_y >> 18);
            if (tmp_end < (s32)next_frame_par->VPP_vd_end_lines_) {
                next_frame_par->VPP_vd_end_lines_ = tmp_end;
            }

        } else if(tmp_bottom > (s32)height_out && tmp_top >= (s32)height_out){
            next_frame_par->VPP_vd_end_lines_ = 1;
        }
        next_frame_par->VPP_vd_end_lines_ = next_frame_par->VPP_vd_end_lines_ - h_in /  height_out;
        if ((s32)next_frame_par->VPP_vd_end_lines_ < (s32)next_frame_par->VPP_vd_start_lines_) {
            next_frame_par->VPP_vd_end_lines_ = next_frame_par->VPP_vd_start_lines_;
        }
        if ((s32)next_frame_par->VPP_hd_end_lines_ < (s32)next_frame_par->VPP_hd_start_lines_) {
            next_frame_par->VPP_hd_end_lines_ = next_frame_par->VPP_hd_start_lines_;
        }
        //printk("tmp_top %d tmp_bottom %d tmp_height %d\n", tmp_top, tmp_bottom, tmp_height);
        //printk("vf2 %d %d %d %d\n", next_frame_par->VPP_hd_start_lines_, next_frame_par->VPP_hd_end_lines_,
        //        next_frame_par->VPP_vd_start_lines_, next_frame_par->VPP_vd_end_lines_);
    }
#endif
}

#ifdef TV_3D_FUNCTION_OPEN

static void
vpp_get_video_source_size(u32 *src_width,u32 *src_height,u32 process_3d_type,
					vframe_t *vf, vpp_frame_par_t *next_frame_par)
{

   if (process_3d_type&MODE_3D_AUTO) {

        if(vf->trans_fmt) {
	    if(process_3d_type & MODE_3D_TO_2D_MASK) {
	        *src_height = vf->left_eye.height;
            } else {
	        *src_height = vf->left_eye.height<<1;
	        next_frame_par->vpp_2pic_mode = 1;
            }
	    *src_width  = vf->left_eye.width;
   	}

        switch(vf->trans_fmt){
            case TVIN_TFMT_3D_LRH_OLOR:
	    case TVIN_TFMT_3D_LRH_OLER:
	    case TVIN_TFMT_3D_LRH_ELOR:
	    case TVIN_TFMT_3D_LRH_ELER:
	    case TVIN_TFMT_3D_DET_LR:
	        next_frame_par->vpp_3d_mode = VPP_3D_MODE_LR;
	        break;
            case TVIN_TFMT_3D_FP:
            case TVIN_TFMT_3D_TB:
	    case TVIN_TFMT_3D_DET_TB:
	    case TVIN_TFMT_3D_FA:
	        next_frame_par->vpp_3d_mode = VPP_3D_MODE_TB;
		/*just for mvc 3d file*/
		if (process_3d_type&MODE_3D_MVC) {
		    next_frame_par->vpp_2pic_mode = 2;
		    next_frame_par->vpp_3d_mode = VPP_3D_MODE_FA;
		}
                break;
	    case TVIN_TFMT_3D_LA:
            case TVIN_TFMT_3D_DET_INTERLACE:
		next_frame_par->vpp_3d_mode = VPP_3D_MODE_LA;
		next_frame_par->vpp_2pic_mode = 0;
		break;
            case TVIN_TFMT_3D_DET_CHESSBOARD:
	    default:
	        *src_width  = vf->width;
                *src_height = vf->height;
		next_frame_par->vpp_3d_mode = VPP_3D_MODE_NULL;
		next_frame_par->vpp_3d_scale = 1;
		next_frame_par->vpp_2pic_mode = 0;
                break;
        }

    } else if (process_3d_type&MODE_3D_LR) {
        next_frame_par->vpp_3d_mode = VPP_3D_MODE_LR;
	if(process_3d_type & MODE_3D_TO_2D_MASK) {
	    *src_width  = vf->width>>1;
            *src_height = vf->height;
	} else {
	    *src_width  = vf->width>>1;
	    *src_height = vf->height<<1;
	    next_frame_par->vpp_2pic_mode = 1;
	}

    } else if (process_3d_type & MODE_3D_TB) {
        next_frame_par->vpp_3d_mode = VPP_3D_MODE_TB;
	if(process_3d_type & MODE_3D_TO_2D_MASK){
	    *src_width  = vf->width;
	    *src_height = vf->height>>1;
	} else {
	    *src_width  = vf->width;
            *src_height = vf->height;
	    next_frame_par->vpp_2pic_mode = 1;
	}
    } else if(process_3d_type & MODE_3D_LA) {
    	next_frame_par->vpp_3d_mode = VPP_3D_MODE_LA;
	if((process_3d_type & MODE_3D_LR_SWITCH)||(process_3d_type & MODE_3D_TO_2D_L))
	     *src_height = vf->height+1;
	else
	    *src_height = vf->height-1 ;
	*src_width  = vf->width;
	next_frame_par->vpp_2pic_mode = 0;
        next_frame_par->vpp_3d_scale = 1;
	if(process_3d_type & MODE_3D_TO_2D_MASK) {
	    next_frame_par->vscale_skip_count = 1;
	    next_frame_par->vpp_3d_scale = 0;
	}
    } else if(process_3d_type & MODE_3D_FA) {
        next_frame_par->vpp_3d_mode = VPP_3D_MODE_FA;
	if(process_3d_type & MODE_3D_TO_2D_MASK) {
	    *src_width  = vf->width;
	    *src_height = vf->height;
        } else {
    	    *src_width  = vf->width;
	    *src_height = vf->height<<1;
	    next_frame_par->vpp_2pic_mode = 2;
	}
    } else {
	*src_width  = vf->width;
        *src_height = vf->height;
	next_frame_par->vpp_3d_mode = VPP_3D_MODE_NULL;
	next_frame_par->vpp_2pic_mode = 0;
	next_frame_par->vpp_3d_scale = 0;
    }
	/*process 3d->2d or l/r switch case*/
    if((VPP_3D_MODE_NULL != next_frame_par->vpp_3d_mode) &&
       (VPP_3D_MODE_LA != next_frame_par->vpp_3d_mode)     )
    {
	if(process_3d_type & MODE_3D_TO_2D_R)
       	    next_frame_par->vpp_2pic_mode = VPP_SELECT_PIC1;
	else if(process_3d_type & MODE_3D_TO_2D_L)
	    next_frame_par->vpp_2pic_mode = VPP_SELECT_PIC0;
	else if(process_3d_type & MODE_3D_LR_SWITCH)
	    next_frame_par->vpp_2pic_mode |= VPP_PIC1_FIRST;

	/*only display one pic*/
	if((next_frame_par->vpp_2pic_mode & 0x3) == 0)
	    next_frame_par->vpp_3d_scale = 0;
	else
	    next_frame_par->vpp_3d_scale = 1;
    }
    /*avoid dividing 0 error*/
    if(*src_width==0 || *src_height==0){
	*src_width  = vf->width;
       	*src_height = vf->height;
    }
}
#endif
void
vpp_set_filters(u32 process_3d_type,u32 wide_mode,
                vframe_t *vf,
                vpp_frame_par_t *next_frame_par,
                const vinfo_t *vinfo)
{
    u32 src_width = 0;
    u32 src_height = 0;
    u32 vpp_flags = 0;
    u32 aspect_ratio = 0;

    BUG_ON(vinfo == NULL);

    next_frame_par->VPP_post_blend_vd_v_start_ = 0;
    next_frame_par->VPP_post_blend_vd_h_start_ = 0;

    next_frame_par->VPP_postproc_misc_ = 0x200;
#ifdef TV_3D_FUNCTION_OPEN
    next_frame_par->vscale_skip_count = 0;
    /*
    *check 3d mode change in display buffer or 3d type
    *get the source size according to 3d mode
    */
    if(process_3d_type & MODE_3D_ENABLE) {
        vpp_get_video_source_size(&src_width,&src_height,process_3d_type,vf,next_frame_par);
    } else {
        src_width = vf->width;
        src_height = vf->height;
        next_frame_par->vpp_3d_mode = VPP_3D_MODE_NULL;
        next_frame_par->vpp_2pic_mode = 0;
	next_frame_par->vpp_3d_scale = 0;
    }

    if(vpp_3d_scale){
	next_frame_par->vpp_3d_scale = 1;
    }
	amlog_mask(LOG_MASK_VPP,"%s: src_width %u,src_height %u.\n",__func__,src_width,src_height);
#endif
    /* check force ratio change flag in display buffer also
     * if it exist then it will override the settings in display side
     */
    if (vf->ratio_control & DISP_RATIO_FORCECONFIG) {
        if ((vf->ratio_control & DISP_RATIO_CTRL_MASK) == DISP_RATIO_KEEPRATIO) {
            if (wide_mode == VIDEO_WIDEOPTION_FULL_STRETCH) {
                wide_mode = VIDEO_WIDEOPTION_NORMAL;
            }
        }else {
            if (wide_mode == VIDEO_WIDEOPTION_NORMAL) {
                wide_mode = VIDEO_WIDEOPTION_FULL_STRETCH;
            }
        }
        if (vf->ratio_control & DISP_RATIO_FORCE_NORMALWIDE)
            wide_mode = VIDEO_WIDEOPTION_NORMAL;
        else if (vf->ratio_control & DISP_RATIO_FORCE_FULL_STRETCH)
            wide_mode = VIDEO_WIDEOPTION_FULL_STRETCH;
    }

    aspect_ratio = (vf->ratio_control & DISP_RATIO_ASPECT_RATIO_MASK)
                   >> DISP_RATIO_ASPECT_RATIO_BIT;

    if (vf->type & VIDTYPE_INTERLACE) {
        vpp_flags = VPP_FLAG_INTERLACE_IN;
    }

    if(vf->ratio_control & DISP_RATIO_PORTRAIT_MODE){
        vpp_flags |= VPP_FLAG_PORTRAIT_MODE;
    }

    if(vf->type & VIDTYPE_VSCALE_DISABLE){
        vpp_flags |= VPP_FLAG_VSCALE_DISABLE;
    }

    src_width = vf->width;
    src_height = vf->height;

    vpp_wide_mode = wide_mode;
    vpp_flags |= wide_mode | (aspect_ratio << VPP_FLAG_AR_BITS);

    if (vinfo->field_height != vinfo->height) {
        vpp_flags |= VPP_FLAG_INTERLACE_OUT;
    }

    next_frame_par->VPP_post_blend_vd_v_end_ = vinfo->field_height - 1;
    next_frame_par->VPP_post_blend_vd_h_end_ = vinfo->width - 1;
    next_frame_par->VPP_post_blend_h_size_ = vinfo->width;

    vpp_set_filters2(src_width, src_height, vinfo, vpp_flags, next_frame_par);
}

#if HAS_VPU_PROT
void
prot_get_parameter(u32 wide_mode,
                vframe_t *vf,
                vpp_frame_par_t *next_frame_par,
                const vinfo_t *vinfo)
{
    u32 src_width = 0;
    u32 src_height = 0;
    u32 vpp_flags = 0;
    u32 aspect_ratio = 0;

    BUG_ON(vinfo == NULL);

    next_frame_par->VPP_post_blend_vd_v_start_ = 0;
    next_frame_par->VPP_post_blend_vd_h_start_ = 0;

    next_frame_par->VPP_postproc_misc_ = 0x200;

    /* check force ratio change flag in display buffer also
     * if it exist then it will override the settings in display side
     */
    if (vf->ratio_control & DISP_RATIO_FORCECONFIG) {
        if ((vf->ratio_control & DISP_RATIO_CTRL_MASK) == DISP_RATIO_KEEPRATIO) {
            if (wide_mode == VIDEO_WIDEOPTION_FULL_STRETCH) {
                wide_mode = VIDEO_WIDEOPTION_NORMAL;
            }
        }else {
            if (wide_mode == VIDEO_WIDEOPTION_NORMAL) {
                wide_mode = VIDEO_WIDEOPTION_FULL_STRETCH;
            }
        }
        if (vf->ratio_control & DISP_RATIO_FORCE_NORMALWIDE)
            wide_mode = VIDEO_WIDEOPTION_NORMAL;
        else if (vf->ratio_control & DISP_RATIO_FORCE_FULL_STRETCH)
            wide_mode = VIDEO_WIDEOPTION_FULL_STRETCH;
    }

    aspect_ratio = (vf->ratio_control & DISP_RATIO_ASPECT_RATIO_MASK)
                   >> DISP_RATIO_ASPECT_RATIO_BIT;

    if (vf->type & VIDTYPE_INTERLACE) {
        vpp_flags = VPP_FLAG_INTERLACE_IN;
    }

    if(vf->ratio_control & DISP_RATIO_PORTRAIT_MODE){
        vpp_flags |= VPP_FLAG_PORTRAIT_MODE;
    }

    if(vf->type & VIDTYPE_VSCALE_DISABLE){
        vpp_flags |= VPP_FLAG_VSCALE_DISABLE;
    }

    src_width = vf->width;
    src_height = vf->height;

    vpp_wide_mode = wide_mode;
    vpp_flags |= wide_mode | (aspect_ratio << VPP_FLAG_AR_BITS);

    if (vinfo->field_height != vinfo->height) {
        vpp_flags |= VPP_FLAG_INTERLACE_OUT;
    }

    next_frame_par->VPP_post_blend_vd_v_end_ = vinfo->field_height - 1;
    next_frame_par->VPP_post_blend_vd_h_end_ = vinfo->width - 1;
    next_frame_par->VPP_post_blend_h_size_ = vinfo->width;

    vpp_set_filters2(src_width, src_height, vinfo, vpp_flags, next_frame_par);
}
#endif

void vpp_set_osd_layer_preblend(u32 *enable)
{
	osd_layer_preblend=*enable;
}

void vpp_set_osd_layer_position(s32  *para)
{
	if(IS_ERR_OR_NULL(&para[3]))
	{
		printk("para[3] is null\n");
		return ;
	}
	if(para[2] < 2 || para[3] < 2) return ;

	osd_layer_left=para[0];
	osd_layer_top=para[1];
	osd_layer_width=para[2];
	osd_layer_height=para[3];
}

void vpp_set_video_source_crop(u32 t, u32 l, u32 b, u32 r)
{
    video_source_crop_top = t;
    video_source_crop_left = l;
    video_source_crop_bottom = b;
    video_source_crop_right = r;
}

void vpp_get_video_source_crop(u32 *t, u32 *l, u32 *b, u32 *r)
{
    *t = video_source_crop_top;
    *l = video_source_crop_left;
    *b = video_source_crop_bottom;
    *r = video_source_crop_right;
}

void vpp_set_video_layer_position(s32 x, s32 y, s32 w, s32 h)
{
    if ((w < 0) || (h < 0)) {
        return;
    }

    video_layer_left = x;
    video_layer_top = y;
    video_layer_width = w;
    video_layer_height = h;
}

void vpp_get_video_layer_position(s32 *x, s32 *y, s32 *w, s32 *h)
{
    *x = video_layer_left;
    *y = video_layer_top;
    *w = video_layer_width;
    *h = video_layer_height;
}

void vpp_set_global_offset(s32 x, s32 y)
{
    video_layer_global_offset_x = x;
    video_layer_global_offset_y = y;
}

void vpp_get_global_offset(s32 *x, s32 *y)
{
    *x = video_layer_global_offset_x;
    *y = video_layer_global_offset_y;
}

s32 vpp_set_nonlinear_factor(u32 f)
{
    if (f < MAX_NONLINEAR_FACTOR) {
        nonlinear_factor = f;
        return 0;
    }
    return -1;
}

u32 vpp_get_nonlinear_factor(void)
{
    return nonlinear_factor;
}

void vpp_set_zoom_ratio(u32 r)
{
    vpp_zoom_ratio = r;
}

u32 vpp_get_zoom_ratio(void)
{
   return vpp_zoom_ratio;
}
void vpp_set_video_speed_check(u32 h, u32 w)
{
    video_speed_check_height = h;
    video_speed_check_width = w;
}

void vpp_get_video_speed_check(u32 *h, u32 *w)
{
    *h = video_speed_check_height;
    *w = video_speed_check_width;
}
#ifdef TV_3D_FUNCTION_OPEN
void vpp_set_3d_scale(bool enable)
{
	vpp_3d_scale = enable;
}
#endif


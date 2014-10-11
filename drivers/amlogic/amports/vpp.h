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

#ifndef VPP_H
#define VPP_H

#define VPP_FLAG_WIDEMODE_MASK      0x0000000F
#define VPP_FLAG_INTERLACE_OUT      0x00000010
#define VPP_FLAG_INTERLACE_IN       0x00000020
#define VPP_FLAG_CBCR_SEPARATE      0x00000040
#define VPP_FLAG_ZOOM_SHORTSIDE     0x00000080
#define VPP_FLAG_AR_MASK            0x0003ff00
#define VPP_FLAG_AR_BITS            8
#define VPP_FLAG_PORTRAIT_MODE      0x00040000
#define VPP_FLAG_VSCALE_DISABLE     0x00080000

#define IDX_H           (2 << 8)
#define IDX_V_Y         (1 << 13)
#define IDX_V_CBCR      ((1 << 13) | (1 << 8))

#define ASPECT_4_3      ((3<<8)/4)
#define ASPECT_16_9     ((9<<8)/16)

#define SPEED_CHECK_DONE	0
#define SPEED_CHECK_HSKIP	1
#define SPEED_CHECK_VSKIP	2

typedef enum {
    F2V_IT2IT = 0,
    F2V_IB2IB,
    F2V_IT2IB,
    F2V_IB2IT,
    F2V_P2IT,
    F2V_P2IB,
    F2V_IT2P,
    F2V_IB2P,
    F2V_P2P,
    F2V_TYPE_MAX
} f2v_vphase_type_t;   /* frame to video conversion type */

typedef struct {
    s8 repeat_skip;
    u8 phase;
} f2v_vphase_t;

typedef struct {
    u32  vpp_hf_start_phase_step;
    u32  vpp_hf_start_phase_slope;
    u32  vpp_hf_end_phase_slope;

    const u32 *vpp_vert_coeff;
    const u32 *vpp_horz_coeff;
    u32  vpp_sc_misc_;
    u32  vpp_vsc_start_phase_step;
    u32  vpp_hsc_start_phase_step;
} vppfilter_mode_t;

typedef struct {
    vppfilter_mode_t *top;
    vppfilter_mode_t *bottom;
} vpp_filters_t;

typedef struct {
    u32 VPP_hd_start_lines_;
    u32 VPP_hd_end_lines_;
    u32 VPP_vd_start_lines_;
    u32 VPP_vd_end_lines_;

    u32 VPP_vsc_startp;
    u32 VPP_vsc_endp;

    u32 VPP_hsc_startp;
    u32 VPP_hsc_linear_startp;
    u32 VPP_hsc_linear_endp;
    u32 VPP_hsc_endp;

    u32 VPP_hf_ini_phase_;
    f2v_vphase_t VPP_vf_ini_phase_[9];

    u32 VPP_pic_in_height_;
    u32 VPP_line_in_length_;

    u32 VPP_post_blend_vd_v_start_;
    u32 VPP_post_blend_vd_v_end_;
    u32 VPP_post_blend_vd_h_start_;
    u32 VPP_post_blend_vd_h_end_;
    u32 VPP_post_blend_h_size_;

    vppfilter_mode_t vpp_filter;

    u32 VPP_postproc_misc_;
    u32 vscale_skip_count;
    u32 hscale_skip_count;
    u32 vpp_3d_mode;
    u32 vpp_2pic_mode;//bit[1:0] 0: 1 pic,1:two pic one buf,2:tow pic two buf
    		      //bit[2]0:select pic0,1:select pic1
    		      //bit[3]0:pic0 first,1:pic1 first
    bool vpp_3d_scale;

} vpp_frame_par_t;

#if (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TV)||(MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TVD)
#define TV_3D_FUNCTION_OPEN
#endif

#ifdef TV_3D_FUNCTION_OPEN

/*cmd use for 3d operation*/
#define MODE_3D_DISABLE     0x00000000
#define MODE_3D_ENABLE      0x00000001
#define MODE_3D_AUTO        0x00000002
#define MODE_3D_LR          0x00000004
#define MODE_3D_TB          0x00000008
#define MODE_3D_LA          0x00000010
#define MODE_3D_FA          0x00000020
#define MODE_3D_LR_SWITCH   0x00000100
#define MODE_3D_TO_2D_L     0x00000200
#define MODE_3D_TO_2D_R     0x00000400
#define MODE_3D_MVC	    0x00000800
#define MODE_3D_TO_2D_MASK  (MODE_3D_TO_2D_L|MODE_3D_TO_2D_R)

#define VPP_3D_MODE_NULL 0x0
#define VPP_3D_MODE_LR   0x1
#define VPP_3D_MODE_TB   0x2
#define VPP_3D_MODE_LA	 0x3
#define VPP_3D_MODE_FA	 0x4

#define VPP_SELECT_PIC0  0x0
#define VPP_SELECT_PIC1  0x4

#define VPP_PIC0_FIRST	0x0
#define VPP_PIC1_FIRST	0x8

extern
void vpp_set_3d_scale(bool enable);

#endif

extern void
vpp_set_filters(u32 process_3d_type,u32 wide_mode, vframe_t * vf,
                vpp_frame_par_t * next_frame_par, const vinfo_t *vinfo);


extern void
vpp_set_video_source_crop(u32 t, u32 l, u32 b, u32 r);

extern void
vpp_get_video_source_crop(u32 *t, u32 *l, u32 *b, u32 *r);

extern void
vpp_set_video_layer_position(s32 x, s32 y, s32 w, s32 h);

extern void
vpp_get_video_layer_position(s32 *x, s32 *y, s32 *w, s32 *h);

extern void
vpp_set_global_offset(s32 x, s32 y);

extern void
vpp_get_global_offset(s32 *x, s32 *y);

extern void
vpp_set_zoom_ratio(u32 r);

extern u32
vpp_get_zoom_ratio(void);

extern void 
vpp_set_osd_layer_preblend(u32*);

extern 
void vpp_set_osd_layer_position(s32*);

extern
s32 vpp_set_nonlinear_factor(u32 f);

extern
u32 vpp_get_nonlinear_factor(void);
extern
void vpp_set_video_speed_check(u32 h, u32 w);

extern
void vpp_get_video_speed_check(u32 *h, u32 *w);

#ifdef CONFIG_AM_VIDEO2
extern void
vpp2_set_filters(u32 wide_mode, vframe_t * vf,
                vpp_frame_par_t * next_frame_par, const vinfo_t *vinfo);

extern void
vpp2_set_video_layer_position(s32 x, s32 y, s32 w, s32 h);

extern void
vpp2_get_video_layer_position(s32 *x, s32 *y, s32 *w, s32 *h);

extern void
vpp2_set_zoom_ratio(u32 r);

extern u32
vpp2_get_zoom_ratio(void);
#endif


#endif /* VPP_H */

/*
 * Amlogic Meson
 * frame buffer driver
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * Author:  Amlogic R&D Group
 *
 */
#include <linux/amlogic/amports/video_prot.h>

static int set_prot_NV21(u32 x_start, u32 x_end, u32 y_start, u32 y_end, u32 y_step, u32 angle, u32 pat_val, u32 prot2_canvas, u32 prot3_canvas) {

    u32 data32;
    if (angle == 0 || angle == 2) {
           data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (0 << 4) | (0 << 3) | (0 << 2) | (0 << 0);
           VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, data32);
           data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (1 << 4) | (0 << 3) | (0 << 2) | (0 << 0);
           VSYNC_WR_MPEG_REG(VPU_PROT3_GEN_CNTL, data32);
           VSYNC_WR_MPEG_REG(VD1_IF0_PROT_CNTL, 0);
           return 0;
    } else {
        u32 x_start_uv = x_start >> 1;
        u32 x_end_uv = x_end >> 1;
        u32 y_start_uv = y_start >> 1;
        u32 y_end_uv = y_end >> 1;
        u32 y_len = (y_end - y_start) / (y_step + 1);
        u32 y_len_uv = (y_end_uv - y_start_uv) / (y_step + 1);

        y_end = y_start + (y_step + 1) * y_len;
        y_end_uv = y_start_uv + (y_step + 1) * y_len_uv;

        data32 = (x_end << 16) | (x_start << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_X_START_END, data32);
        data32 = (x_end_uv << 16) | (x_start_uv << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT3_X_START_END, data32);

        data32 = (y_end << 16) | (y_start << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_Y_START_END, data32);
        data32 = (y_end_uv << 16) | (y_start_uv << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT3_Y_START_END, data32);

        data32 = (y_step << 16) | (y_len << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_Y_LEN_STEP, data32);
        data32 = (y_step << 16) | (y_len_uv << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT3_Y_LEN_STEP, data32);

        data32 = (PAT_START_PTR << 4) | (PAT_END_PTR << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_RPT_LOOP, data32);
        VSYNC_WR_MPEG_REG(VPU_PROT3_RPT_LOOP, data32);
        VSYNC_WR_MPEG_REG(VPU_PROT2_RPT_PAT, pat_val);
        VSYNC_WR_MPEG_REG(VPU_PROT3_RPT_PAT, pat_val);
        data32 = (CUGT << 20) | (CID_MODE << 16) | (CID_VALUE << 8) | prot2_canvas;
        VSYNC_WR_MPEG_REG(VPU_PROT2_DDR, data32);
        data32 = (CUGT << 20) | (CID_MODE << 16) | (CID_VALUE << 8) | prot3_canvas;
        VSYNC_WR_MPEG_REG(VPU_PROT3_DDR, data32);
        data32 = (REQ_ONOFF_EN << 31) | (REQ_OFF_MIN << 16) | (REQ_ON_MAX << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_REQ_ONOFF, data32);
        VSYNC_WR_MPEG_REG(VPU_PROT3_REQ_ONOFF, data32);

        if (angle == 1) {
            data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (0 << 4) | (1 << 3) | (0 << 2) | (1 << 0);
            VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, data32);
            data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (1 << 4) | (1 << 3) | (0 << 2) | (1 << 0);
            VSYNC_WR_MPEG_REG(VPU_PROT3_GEN_CNTL, data32);
        } else if (angle == 3) {
            data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (0 << 4) | (0 << 3) | (1 << 2) | (1 << 0);
            VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, data32);
            data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (1 << 4) | (0 << 3) | (1 << 2) | (1 << 0);
            VSYNC_WR_MPEG_REG(VPU_PROT3_GEN_CNTL, data32);
        }
        VSYNC_WR_MPEG_REG(VD1_IF0_PROT_CNTL, 1 << 31 | 1080 << 16 | 1080);
    }
    return 0;
}

static int set_prot_422(u32 x_start, u32 x_end, u32 y_start, u32 y_end, u32 y_step, u32 angle, u32 pat_val, u32 prot2_canvas, u32 prot3_canvas) {

    u32 data32;

    if (angle == 0 || angle == 2) {
           data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (0 << 4) | (0 << 3) | (0 << 2) | (0 << 0);
           VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, data32);
           data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (1 << 4) | (0 << 3) | (0 << 2) | (0 << 0);
           VSYNC_WR_MPEG_REG(VPU_PROT3_GEN_CNTL, data32);
           VSYNC_WR_MPEG_REG(VD1_IF0_PROT_CNTL, 0);
           return 0;
    } else {
        u32 x_start_uv = x_start >> 1;
        u32 x_end_uv = x_end >> 1;
        u32 y_start_uv = y_start >> 1;
        u32 y_end_uv = y_end;
        u32 y_len = (y_end - y_start) / (y_step + 1);
        u32 y_len_uv = (y_end_uv - y_start_uv) / (y_step + 1);

        y_end = y_start + (y_step + 1) * y_len;
        y_end_uv = y_start_uv + (y_step + 1) * y_len_uv;

        data32 = (x_end << 16) | (x_start << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_X_START_END, data32);
        data32 = (x_end_uv << 16) | (x_start_uv << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT3_X_START_END, data32);

        data32 = (y_end << 16) | (y_start << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_Y_START_END, data32);
        data32 = (y_end_uv << 16) | (y_start_uv << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT3_Y_START_END, data32);

        data32 = (y_step << 16) | (y_len << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_Y_LEN_STEP, data32);
        data32 = (y_step << 16) | (y_len_uv << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT3_Y_LEN_STEP, data32);

        data32 = (PAT_START_PTR << 4) | (PAT_END_PTR << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_RPT_LOOP, data32);
        VSYNC_WR_MPEG_REG(VPU_PROT3_RPT_LOOP, data32);
        VSYNC_WR_MPEG_REG(VPU_PROT2_RPT_PAT, pat_val);
        VSYNC_WR_MPEG_REG(VPU_PROT3_RPT_PAT, pat_val);
        data32 = (CUGT << 20) | (CID_MODE << 16) | (CID_VALUE << 8) | prot2_canvas;
        VSYNC_WR_MPEG_REG(VPU_PROT2_DDR, data32);
        data32 = (CUGT << 20) | (CID_MODE << 16) | (CID_VALUE << 8) | prot3_canvas;
        VSYNC_WR_MPEG_REG(VPU_PROT3_DDR, data32);
        data32 = (REQ_ONOFF_EN << 31) | (REQ_OFF_MIN << 16) | (REQ_ON_MAX << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_REQ_ONOFF, data32);
        VSYNC_WR_MPEG_REG(VPU_PROT3_REQ_ONOFF, data32);

        if (angle == 1) {
            data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (0 << 4) | (1 << 3) | (0 << 2) | (1 << 0);
            VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, data32);
            data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (1 << 4) | (1 << 3) | (0 << 2) | (1 << 0);
            VSYNC_WR_MPEG_REG(VPU_PROT3_GEN_CNTL, data32);
        } else if (angle == 3) {
            data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (0 << 4) | (0 << 3) | (1 << 2) | (1 << 0);
            VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, data32);
            data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (1 << 4) | (0 << 3) | (1 << 2) | (1 << 0);
            VSYNC_WR_MPEG_REG(VPU_PROT3_GEN_CNTL, data32);
        }
        VSYNC_WR_MPEG_REG(VD1_IF0_PROT_CNTL, 1 << 31 | 1080 << 16 | 1080);
    }
    return 0;
}

static int set_prot_444(u32 x_start, u32 x_end, u32 y_start, u32 y_end, u32 y_step, u32 angle, u32 pat_val, u32 prot2_canvas) {

    u32 data32;

    if (angle == 0 || angle == 2) {
           data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (0 << 4) | (0 << 3) | (0 << 2) | (0 << 0);
           VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, data32);
           VSYNC_WR_MPEG_REG(VD1_IF0_PROT_CNTL, 0);
           return 0;
    } else {
        u32 y_len = (y_end - y_start) / (y_step + 1);

        y_end = y_start + (y_step + 1) * y_len;
        data32 = (x_end << 16) | (x_start << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_X_START_END, data32);

        data32 = (y_end << 16) | (y_start << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_Y_START_END, data32);
        data32 = (y_step << 16) | (y_len << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_Y_LEN_STEP, data32);

        data32 = (PAT_START_PTR << 4) | (PAT_END_PTR << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_RPT_LOOP, data32);
        VSYNC_WR_MPEG_REG(VPU_PROT2_RPT_PAT, pat_val);
        data32 = (CUGT << 20) | (CID_MODE << 16) | (CID_VALUE << 8) | prot2_canvas;
        VSYNC_WR_MPEG_REG(VPU_PROT2_DDR, data32);
        data32 = (REQ_ONOFF_EN << 31) | (REQ_OFF_MIN << 16) | (REQ_ON_MAX << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_REQ_ONOFF, data32);

        if (angle == 1) {
            data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (0 << 4) | (1 << 3) | (0 << 2) | (1 << 0);
            VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, data32);
        } else if (angle == 3) {
            data32 = (HOLD_LINES << 8) | (LITTLE_ENDIAN << 7) | (0 << 6) | (0 << 4) | (0 << 3) | (1 << 2) | (1 << 0);
            VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, data32);
        }
        VSYNC_WR_MPEG_REG(VD1_IF0_PROT_CNTL, 1 << 31 | 1080 << 16 | 1080);
    }
    return 0;
}

void video_prot_gate_on(void) {
    VSYNC_WR_MPEG_REG(VPU_PROT2_CLK_GATE, 1);
    VSYNC_WR_MPEG_REG(VPU_PROT3_CLK_GATE, 1);
    VSYNC_WR_MPEG_REG_BITS(VPU_PROT2_MMC_CTRL, 1, 12, 3);
    VSYNC_WR_MPEG_REG_BITS(VPU_PROT3_MMC_CTRL, 1, 12, 3);
    VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, 0);
    VSYNC_WR_MPEG_REG(VPU_PROT3_GEN_CNTL, 0);
}

void video_prot_gate_off(void) {
    VSYNC_WR_MPEG_REG(VD1_IF0_PROT_CNTL, 0);
    VSYNC_WR_MPEG_REG(VPU_PROT2_GEN_CNTL, 0);
    VSYNC_WR_MPEG_REG(VPU_PROT3_GEN_CNTL, 0);
    VSYNC_WR_MPEG_REG(VPU_PROT2_CLK_GATE, 0);
    VSYNC_WR_MPEG_REG(VPU_PROT3_CLK_GATE, 0);
    VSYNC_WR_MPEG_REG_BITS(VPU_PROT2_MMC_CTRL, 0, 12, 3);
    VSYNC_WR_MPEG_REG_BITS(VPU_PROT3_MMC_CTRL, 0, 12, 3);
}

void video_prot_init(video_prot_t* video_prot, vframe_t *vf) {

    if (vf->width > 1920 || vf->height > 1088) {
        video_prot->is_4k2k = 1;
        video_prot->y_step = 1;
        video_prot->pat_val = 0x80;
    } else {
        video_prot->is_4k2k = 0;
        video_prot->y_step = 0;
        video_prot->pat_val = 0x0;
    }
    video_prot->src_vframe_ratio = (vf->ratio_control & DISP_RATIO_ASPECT_RATIO_MASK) >> DISP_RATIO_ASPECT_RATIO_BIT;
    video_prot->src_vframe_width = vf->width;
    video_prot->src_vframe_height = vf->height;
    video_prot->x_start = 0;
    video_prot->y_start = 0;
    video_prot->x_end = vf->width - 1;
    video_prot->y_end = vf->height - 1;
    video_prot->viu_type = vf->type;
    video_prot->src_vframe_orientation = vf->orientation;
    video_prot->disable_prot = 0;

}

void video_prot_clear(video_prot_t* video_prot) {

    video_prot->is_4k2k = 0;
    video_prot->y_step = 0;
    video_prot->pat_val = 0x0;
    video_prot->src_vframe_ratio = 0;
    video_prot->src_vframe_width = 0;
    video_prot->src_vframe_height = 0;
    video_prot->x_start = 0;
    video_prot->y_start = 0;
    video_prot->x_end = 0;
    video_prot->y_end = 0;
    video_prot->viu_type = 0;
    video_prot->src_vframe_orientation = 0;
    video_prot->angle_changed = 0;
    video_prot->angle = 0;
    video_prot->status = 0;
    video_prot->enable_layer = 0;
}

void video_prot_set_angle(video_prot_t* video_prot, u32 angle_orientation) {
    if (video_prot->viu_type & VIDTYPE_VIU_NV21) {
        set_prot_NV21(video_prot->x_start, video_prot->x_end, video_prot->y_start, video_prot->y_end, video_prot->y_step, angle_orientation, video_prot->pat_val, video_prot->prot2_canvas, video_prot->prot3_canvas);
    } else if (video_prot->viu_type & VIDTYPE_VIU_422) {
        set_prot_422(video_prot->x_start, video_prot->x_end, video_prot->y_start, video_prot->y_end, video_prot->y_step, angle_orientation, video_prot->pat_val, video_prot->prot2_canvas, video_prot->prot3_canvas);
    } else if (video_prot->viu_type & VIDTYPE_VIU_444) {
        set_prot_444(video_prot->x_start, video_prot->x_end, video_prot->y_start, video_prot->y_end, video_prot->y_step, angle_orientation, video_prot->pat_val, video_prot->prot2_canvas);
    } else {
        set_prot_NV21(0, video_prot->x_end, 0, video_prot->y_end, video_prot->y_step, 0, video_prot->pat_val, video_prot->prot2_canvas, video_prot->prot3_canvas);
    }
}

void video_prot_revert_vframe(video_prot_t* video_prot, vframe_t *vf) {
    u32 angle_orientation = video_prot->angle;

    if (video_prot->viu_type & (VIDTYPE_VIU_444 | VIDTYPE_VIU_422 | VIDTYPE_VIU_NV21)) {
        if (angle_orientation == 1 || angle_orientation == 3) {
            if (video_prot->is_4k2k) {
                vf->width = video_prot->src_vframe_height / (video_prot->y_step + 1);
                vf->height = video_prot->src_vframe_width >> 1;
            } else {
                vf->width = video_prot->src_vframe_height;
                vf->height = video_prot->src_vframe_width;
            }
            if (video_prot->src_vframe_ratio != 0) {
                vf->ratio_control &= ~DISP_RATIO_ASPECT_RATIO_MASK;
                vf->ratio_control |= (0x10000 / video_prot->src_vframe_ratio) << DISP_RATIO_ASPECT_RATIO_BIT;
            }
            vf->ratio_control |= DISP_RATIO_PORTRAIT_MODE;
        } else if (angle_orientation == 0 || angle_orientation == 2) {
            vf->width = video_prot->src_vframe_width;
            vf->height = video_prot->src_vframe_height;
            if (video_prot->src_vframe_ratio != 0) {
                vf->ratio_control &= ~DISP_RATIO_ASPECT_RATIO_MASK;
                vf->ratio_control |= video_prot->src_vframe_ratio << DISP_RATIO_ASPECT_RATIO_BIT;
            }
            vf->ratio_control &= ~DISP_RATIO_PORTRAIT_MODE;
        }
    }
}

void video_prot_set_canvas(vframe_t *vf) {
    VSYNC_WR_MPEG_REG_BITS(VPU_PROT2_DDR, vf->canvas0Addr & 0xff, 0, 8);
    if (!(vf->type & VIDTYPE_VIU_444)) {
        VSYNC_WR_MPEG_REG_BITS(VPU_PROT3_DDR, (vf->canvas0Addr >> 8) & 0xff, 0, 8);
    }
}
void video_prot_axis (video_prot_t* video_prot, u32 hd_start, u32 hd_end, u32 vd_start, u32 vd_end) {
    u32 reset_axis = 0;
    u32 angle_orientation = video_prot->angle;

    if (vd_start > 0 || hd_start > 0) {
        if (angle_orientation == 1) {
            if (video_prot->is_4k2k) {
                video_prot->x_start = vd_start << 1;
                video_prot->x_end = video_prot->src_vframe_width - 1;
                video_prot->y_start = 0;
                video_prot->y_end = (video_prot->src_vframe_height - hd_start - 1) * (video_prot->y_step + 1);
            } else {
                video_prot->x_start = vd_start;
                video_prot->x_end = video_prot->src_vframe_width - 1;
                video_prot->y_start = 0;
                video_prot->y_end = video_prot->src_vframe_height - hd_start - 1;
            }
            reset_axis = 1;
        } else if (angle_orientation == 3) {
            if (video_prot->is_4k2k) {
                video_prot->x_start = 0;
                video_prot->x_end = (video_prot->src_vframe_width - vd_start) << 1;
                video_prot->y_start = hd_start * (video_prot->y_step + 1);
                video_prot->y_end = video_prot->src_vframe_height - 1;
            } else {
                video_prot->x_start = 0;
                video_prot->x_end = video_prot->src_vframe_width - vd_start;
                video_prot->y_start = hd_start;
                video_prot->y_end = video_prot->src_vframe_height - 1;
            }
            reset_axis = 1;
        }
        if ((s32)video_prot->x_end < (s32)video_prot->x_start) {
            video_prot->x_end = video_prot->x_start;
        }
        if ((s32)video_prot->y_end < (s32)video_prot->y_start) {
            video_prot->y_end = video_prot->y_start;
        }
    } else {
        video_prot->x_start = 0;
        video_prot->y_start = 0;
        video_prot->x_end = video_prot->src_vframe_width - 1;
        video_prot->y_end = video_prot->src_vframe_height - 1;
        reset_axis = 1;
    }
    //printk("x :%d :%d y :%d :%d\n", video_prot->x_start, video_prot->x_end, video_prot->y_start, video_prot->y_end);
    if (reset_axis) {
        u32 data32;
        u32 x_start = video_prot->x_start;
        u32 x_end = video_prot->x_end;
        u32 y_start = video_prot->y_start;
        u32 y_end = video_prot->y_end;
        u32 y_step = video_prot->y_step;
        u32 y_len = (y_end - y_start) / (y_step + 1);
        u32 x_start_uv = 0;
        u32 x_end_uv = 0;
        u32 y_start_uv = 0;
        u32 y_end_uv = 0;
        u32 y_len_uv = 0;

        reset_axis = 0;
        y_end = y_start + (y_step + 1) * y_len;
        if (video_prot->viu_type & VIDTYPE_VIU_NV21) {
            x_start_uv = x_start >> 1;
            x_end_uv = x_end >> 1;
            y_start_uv = y_start >> 1;
            y_end_uv = y_end >> 1;
            y_len_uv = (y_end_uv - y_start_uv) / (y_step + 1);
            y_end_uv = y_start_uv + (y_step + 1) * y_len_uv;
        } else if (video_prot->viu_type & VIDTYPE_VIU_422) {
            x_start_uv = x_start >> 1;
            x_end_uv = x_end >> 1;
            y_start_uv = y_start >> 1;
            y_end_uv = y_end;
            y_len_uv = (y_end_uv - y_start_uv) / (y_step + 1);
            y_end_uv = y_start_uv + (y_step + 1) * y_len_uv;
        }

        data32 = (x_end << 16) | (x_start << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_X_START_END, data32);
        data32 = (x_end_uv << 16) | (x_start_uv << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT3_X_START_END, data32);

        data32 = (y_end << 16) | (y_start << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_Y_START_END, data32);
        data32 = (y_end_uv << 16) | (y_start_uv << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT3_Y_START_END, data32);

        data32 = (y_step << 16) | (y_len << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT2_Y_LEN_STEP, data32);
        data32 = (y_step << 16) | (y_len_uv << 0);
        VSYNC_WR_MPEG_REG(VPU_PROT3_Y_LEN_STEP, data32);
    }
}

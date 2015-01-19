/*
 * VDIN driver
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *         Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __TVIN_VDIN_CTL_H
#define __TVIN_VDIN_CTL_H


#include <linux/amlogic/amports/vframe.h>

#include "vdin_drv.h"


// ***************************************************************************
// *** enum definitions *********************************************
// ***************************************************************************
/*
YUV601:  SDTV BT.601            YCbCr (16~235, 16~240, 16~240)
YUV601F: SDTV BT.601 Full_Range YCbCr ( 0~255,  0~255,  0~255)
YUV709:  HDTV BT.709            YCbCr (16~235, 16~240, 16~240)
YUV709F: HDTV BT.709 Full_Range YCbCr ( 0~255,  0~255,  0~255)
RGBS:                       StudioRGB (16~235, 16~235, 16~235)
RGB:                              RGB ( 0~255,  0~255,  0~255)
 */
#define WR(x,val)                                       WRITE_VCBUS_REG(x+offset,val)
#define WR_BITS(x,val,start,length)                     WRITE_VCBUS_REG_BITS(x+offset,val,start,length)
#define RD(x)                                           READ_VCBUS_REG(x+offset)
#define RD_BITS(x,start,length)                         READ_VCBUS_REG_BITS(x+offset,start,length)

#if !defined(VDIN_V2)
#define WRITE_VCBUS_REG(x,val)                          WRITE_CBUS_REG(x,val)
#define WRITE_VCBUS_REG_BITS(x,val,start,length)        WRITE_CBUS_REG_BITS(x,val,start,length)
#define READ_VCBUS_REG(x)                               READ_CBUS_REG(x)
#define READ_VCBUS_REG_BITS(x,start,length)             READ_CBUS_REG_BITS(x,start,length)
#endif

typedef enum vdin_matrix_csc_e {
        VDIN_MATRIX_NULL = 0,
        VDIN_MATRIX_XXX_YUV601_BLACK,
        VDIN_MATRIX_RGB_YUV601,
        VDIN_MATRIX_GBR_YUV601,
        VDIN_MATRIX_BRG_YUV601,
        VDIN_MATRIX_YUV601_RGB,
        VDIN_MATRIX_YUV601_GBR,
        VDIN_MATRIX_YUV601_BRG,
        VDIN_MATRIX_RGB_YUV601F,
        VDIN_MATRIX_YUV601F_RGB,
        VDIN_MATRIX_RGBS_YUV601,
        VDIN_MATRIX_YUV601_RGBS,
        VDIN_MATRIX_RGBS_YUV601F,
        VDIN_MATRIX_YUV601F_RGBS,
        VDIN_MATRIX_YUV601F_YUV601,
        VDIN_MATRIX_YUV601_YUV601F,
        VDIN_MATRIX_RGB_YUV709,
        VDIN_MATRIX_YUV709_RGB,
        VDIN_MATRIX_YUV709_GBR,
        VDIN_MATRIX_YUV709_BRG,
        VDIN_MATRIX_RGB_YUV709F,
        VDIN_MATRIX_YUV709F_RGB,
        VDIN_MATRIX_RGBS_YUV709,
        VDIN_MATRIX_YUV709_RGBS,
        VDIN_MATRIX_RGBS_YUV709F,
        VDIN_MATRIX_YUV709F_RGBS,
        VDIN_MATRIX_YUV709F_YUV709,
        VDIN_MATRIX_YUV709_YUV709F,
        VDIN_MATRIX_YUV601_YUV709,
        VDIN_MATRIX_YUV709_YUV601,
        VDIN_MATRIX_YUV601_YUV709F,
        VDIN_MATRIX_YUV709F_YUV601,
        VDIN_MATRIX_YUV601F_YUV709,
        VDIN_MATRIX_YUV709_YUV601F,
        VDIN_MATRIX_YUV601F_YUV709F,
        VDIN_MATRIX_YUV709F_YUV601F,
        VDIN_MATRIX_RGBS_RGB,
        VDIN_MATRIX_RGB_RGBS,
} vdin_matrix_csc_t;

// ***************************************************************************
// *** structure definitions *********************************************
// ***************************************************************************
typedef struct vdin_matrix_lup_s {
        unsigned int pre_offset0_1;
        unsigned int pre_offset2;
        unsigned int coef00_01;
        unsigned int coef02_10;
        unsigned int coef11_12;
        unsigned int coef20_21;
        unsigned int coef22;
        unsigned int post_offset0_1;
        unsigned int post_offset2;
} vdin_matrix_lup_t;

typedef struct vdin_stat_s {
        unsigned int   sum_luma;  // VDIN_HIST_LUMA_SUM_REG
        unsigned int   sum_pixel; // VDIN_HIST_PIX_CNT_REG
} vdin_stat_t;

#ifdef AML_LOCAL_DIMMING
struct ldim_max_s{
    // general parameters
    int ld_pic_rowmax;
    int ld_pic_colmax;
    int ld_stamax_hidx[9];  // U12* 9
    int ld_stamax_vidx[9];  // u12x 9
};
#endif

typedef struct vdin_hist_cfg_s {
        unsigned int                pow;
        unsigned int                win_en;
        unsigned int                rd_en;
        unsigned int                hstart;
        unsigned int                hend;
        unsigned int                vstart;
        unsigned int                vend;
} vdin_hist_cfg_t;

// *****************************************************************************
// ******** GLOBAL FUNCTION CLAIM ********
// *****************************************************************************
extern void vdin_set_vframe_prop_info(struct vframe_s *vf, struct vdin_dev_s *devp);
extern void vdin_get_format_convert(struct vdin_dev_s *devp);
extern void vdin_set_all_regs(struct vdin_dev_s *devp);
extern void vdin_set_default_regmap(unsigned int offset);
extern void vdin_set_def_wr_canvas(struct vdin_dev_s *devp);
extern void vdin_hw_enable(unsigned int offset);
extern void vdin_hw_disable(unsigned int offset);
extern void vdin_set_meas_mux(unsigned int offset, enum tvin_port_e port_);
extern unsigned int vdin_get_field_type(unsigned int offset);
extern void vdin_set_cutwin(struct vdin_dev_s *devp);
extern void vdin_set_decimation(struct vdin_dev_s *devp);
extern unsigned int vdin_get_meas_hcnt64(unsigned int offset);
extern unsigned int vdin_get_meas_vstamp(unsigned int offset);
extern unsigned int vdin_get_active_h(unsigned int offset);
extern unsigned int vdin_get_active_v(unsigned int offset);
extern unsigned int vdin_get_total_v(unsigned int offset);
extern unsigned int vdin_get_canvas_id(unsigned int offset);
extern void vdin_set_canvas_id(unsigned int offset, unsigned int canvas_id);
extern unsigned int vdin_get_chma_canvas_id(unsigned int offset);
extern void vdin_set_chma_canvas_id(unsigned int offset, unsigned int canvas_id);
extern void vdin_enable_module(unsigned int offset, bool enable);
extern void vdin_set_matrix(struct vdin_dev_s *devp);
void vdin_set_matrixs(struct vdin_dev_s *devp, unsigned char no, enum vdin_format_convert_e csc);
extern void vdin_set_matrix_blank(struct vdin_dev_s *devp);
extern void vdin_delay_line(unsigned short num,unsigned int offset);
extern void set_wr_ctrl(int h_pos,int v_pos,struct vdin_dev_s *devp);
extern bool vdin_check_cycle(struct vdin_dev_s *devp);
extern bool vdin_write_done_check(unsigned int offset, struct vdin_dev_s *devp);
extern bool vdin_check_vs(struct vdin_dev_s *devp);
extern void vdin_calculate_duration(struct vdin_dev_s *devp);
extern void vdin_output_ctl(unsigned int offset, unsigned int output_flag);
#if defined(VDIN_V1)
extern void vdin_wr_reverse(unsigned int offset, bool hreverse, bool vreverse);
#endif
extern void vdin_set_hvscale(struct vdin_dev_s *devp);
extern void vdin_set_cm2(unsigned int offset,unsigned int w,unsigned int h,unsigned int *data);
extern void vdin_bypass_isp(unsigned int offset);
extern void vdin_set_mpegin(struct vdin_dev_s *devp);
extern void vdin_force_gofiled(struct vdin_dev_s *devp);

#endif


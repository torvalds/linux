/*
 * ISP driver
 *
 * Author: Kele Bai <kele.bai@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 
 */
#ifndef __TVIN_ISP_HW_H
#define __TVIN_ISP_HW_H

#if 0
#define WR(x,val)                           printk("0x%x <- 0x%x.\n",x,val)
#define WR_BITS(x,val,start,length)    	    printk("0x%x[%u:%u] <- 0x%x.\n",x,start,start+length-1,val)
#define RD(x)                               printk("read 0x%x.\n",x)
#define RD_BITS(x,start,length)             printk("read 0x%x[%u:%u].\n",x,start,start+length-1)
#else
#define WR(x,val)                         WRITE_VCBUS_REG(x,val)
#define WR_BITS(x,val,start,length)       WRITE_VCBUS_REG_BITS(x,val,start,length)
#define RD(x)                             READ_VCBUS_REG(x)
#define RD_BITS(x,start,length)           READ_VCBUS_REG_BITS(x,start,length)

#endif

#define GAMMA_R				0x00000000
#define GAMMA_G				0x00000200
#define GAMMA_B				0x00000400

typedef struct awb_rgb_stat_s {
	unsigned int rgb_count;
	unsigned int rgb_sum[3];// r g b
} awb_rgb_stat_t;

typedef struct awb_yuv_stat_s {
	unsigned int count;
	unsigned int sum;
} awb_yuv_stat_t;

typedef struct isp_awb_stat_s {
	awb_rgb_stat_t rgb;//R G B
	awb_yuv_stat_t yuv_low[4];//y < low    0, -u; 1, +u; 2, -v; 3, +v
	awb_yuv_stat_t yuv_mid[4];
	awb_yuv_stat_t yuv_high[4];//y > high
} isp_awb_stat_t;

typedef struct isp_ae_stat_s {
	unsigned int bayer_over_info[3];//R G B
	unsigned int luma_win[16];
	unsigned char curstep;
	float curgain;
	float maxgain;
	float mingain;
	unsigned char maxstep;
} isp_ae_stat_t;

typedef struct isp_af_stat_s {
	unsigned int bayer_over_info[3];//R G B
	unsigned int luma_win[16];
} isp_af_stat_t;

typedef struct isp_awb_gain_s {
	unsigned int r_val;
	unsigned int g_val;
	unsigned int b_val;
} isp_awb_gain_t;

typedef struct isp_blnr_stat_s {
	unsigned int ac[4];//G0 R1 B2 G3
	unsigned int dc[4];//G0 R1 B2 G3
	unsigned int af_ac[16];
} isp_blnr_stat_t;

extern void isp_wr(unsigned int addr,unsigned int value);
extern unsigned int isp_rd(unsigned int addr);
extern void isp_top_init(xml_top_t *top,unsigned int w,unsigned int h);
extern void isp_set_test_pattern(xml_tp_t *tp);
extern void isp_set_clamp_gain(xml_cg_t *cg);
extern void isp_set_lens_shading(xml_ls_t *ls);
void isp_set_gamma_correction(xml_gc_t *gc);
extern void isp_set_defect_pixel_correction(xml_dp_t *dp);
extern void isp_set_demosaicing(xml_dm_t *dm);
extern void isp_set_matrix(xml_csc_t *csc,unsigned int height);
extern void isp_set_sharpness(xml_sharp_t *sharp);
extern void isp_set_nr(xml_nr_t *nr);
extern void isp_set_awb_stat(xml_awb_t *awb,unsigned int w,unsigned int h);
extern void isp_set_ae_stat(xml_ae_t *ae,unsigned int w,unsigned int h);
extern void isp_set_af_stat(xml_af_t *af,unsigned int w,unsigned int h);
extern void isp_set_af_scan_stat(unsigned int x0,unsigned int y0,unsigned int x1,unsigned int y1);
extern void isp_set_blenr_stat(unsigned int x0,unsigned int y0,unsigned int x1,unsigned int y1);
extern void isp_set_dbg(xml_dbg_t *dbg);
extern void isp_set_lnsd_mode(unsigned int mode);
extern void isp_set_def_config(xml_default_regs_t *regs,tvin_port_t fe_port,tvin_color_fmt_t bfmt,unsigned int w,unsigned int h);
extern void isp_load_def_setting(unsigned int hsize,unsigned int vsize,unsigned char bayer_fmt);
extern void isp_test_pattern(unsigned int hsize,unsigned int vsize,unsigned int htotal,unsigned int vtotal,unsigned char bayer_fmt);
extern void isp_set_manual_wb(xml_wb_manual_t *wb);
extern void isp_get_awb_stat(isp_awb_stat_t *awb_stat);
extern void isp_get_ae_stat(isp_ae_stat_t *ae_stat);
extern void isp_get_af_stat(isp_af_stat_t *af_stat);
extern void isp_get_af_scan_stat(isp_blnr_stat_t *blnr_stat);
extern void isp_get_blnr_stat(isp_blnr_stat_t *blnr_stat);
extern void isp_set_ae_win(unsigned int left, unsigned int right, unsigned int top, unsigned int bottom);
extern void isp_set_awb_win(unsigned int left, unsigned int right, unsigned int top, unsigned int bottom);
extern void isp_set_ae_thrlpf(unsigned char thr_r, unsigned char thr_g, unsigned char thr_b, unsigned char lpf);
extern void isp_set_awb_yuv_thr(unsigned char yh, unsigned char yl, unsigned char u, unsigned char v);
extern void isp_set_awb_rgb_thr(unsigned char gb, unsigned char gr, unsigned br);
extern void flash_on(bool mode_pol_inv,bool led1_pol_inv,bool pin_mux_inv,wave_t *wave_param);
extern void torch_level(bool mode_pol_inv,bool led1_pol_inv,bool pin_mux_inv,bool torch_pol_inv,wave_t *wave_param,unsigned int level);
extern void wave_power_manage(bool enable);
extern void isp_hw_reset(void);
extern void isp_bypass_all(void);
extern void isp_bypass_for_rgb(void);
extern void isp_hw_enable(bool enable);
extern void isp_awb_set_gain(unsigned int r, unsigned int g, unsigned int b);
extern void isp_awb_get_gain(isp_awb_gain_t *awb_gain);
extern void set_isp_gamma_table(unsigned short *gamma,unsigned int type);
extern void get_isp_gamma_table(unsigned short *gamma,unsigned int type);
extern void disable_gc_lns_pk(bool flag);
extern void isp_ls_curve(unsigned int psize_v2h, unsigned int hactive, 
        unsigned int vactive, unsigned int ocenter_c2l, 
        unsigned int ocenter_c2t, unsigned int gain_0db, 
        unsigned int curvature_gr, unsigned int curvature_r, 
        unsigned int curvature_b, unsigned int curvature_gb, bool force_enable);
                  
#endif


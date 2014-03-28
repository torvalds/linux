/*
 * rk3288_drm_fimd.c
 *
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <video/of_display_timing.h>
#include <drm/rockchip_drm.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <linux/rk_fb.h>
#include <video/display_timing.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>
#include "rockchip_drm_drv.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_crtc.h"
#include "rockchip_drm_iommu.h"

/*
 * FIMD is stand for Fully Interactive Mobile Display and
 * as a display controller, it transfers contents drawn on memory
 * to a LCD Panel through Display Interfaces such as RGB or
 * CPU Interface.
 */

/* position control register for hardware window 0, 2 ~ 4.*/
#define VIDOSD_A(win)		(VIDOSD_BASE + 0x00 + (win) * 16)
#define VIDOSD_B(win)		(VIDOSD_BASE + 0x04 + (win) * 16)
/*
 * size control register for hardware windows 0 and alpha control register
 * for hardware windows 1 ~ 4
 */
#define VIDOSD_C(win)		(VIDOSD_BASE + 0x08 + (win) * 16)
/* size control register for hardware windows 1 ~ 2. */
#define VIDOSD_D(win)		(VIDOSD_BASE + 0x0C + (win) * 16)

#define VIDWx_BUF_START(win, buf)	(VIDW_BUF_START(buf) + (win) * 8)
#define VIDWx_BUF_END(win, buf)		(VIDW_BUF_END(buf) + (win) * 8)
#define VIDWx_BUF_SIZE(win, buf)	(VIDW_BUF_SIZE(buf) + (win) * 4)

/* color key control register for hardware window 1 ~ 4. */
#define WKEYCON0_BASE(x)		((WKEYCON0 + 0x140) + ((x - 1) * 8))
/* color key value register for hardware window 1 ~ 4. */
#define WKEYCON1_BASE(x)		((WKEYCON1 + 0x140) + ((x - 1) * 8))

/* FIMD has totally five hardware windows. */
#define WINDOWS_NR	4
/*****************************************************************************************************/
#define SCALE_FACTOR_BILI_DN_FIXPOINT_SHIFT   12   /* 4.12*/
#define SCALE_FACTOR_BILI_DN_FIXPOINT(x)      ((INT32)((x)*(1 << SCALE_FACTOR_BILI_DN_FIXPOINT_SHIFT)))

#define SCALE_FACTOR_BILI_UP_FIXPOINT_SHIFT   16   /* 0.16*/

#define SCALE_FACTOR_AVRG_FIXPOINT_SHIFT   16   /*0.16*/
#define SCALE_FACTOR_AVRG_FIXPOINT(x)      ((INT32)((x)*(1 << SCALE_FACTOR_AVRG_FIXPOINT_SHIFT)))

#define SCALE_FACTOR_BIC_FIXPOINT_SHIFT    16   /* 0.16*/
#define SCALE_FACTOR_BIC_FIXPOINT(x)       ((INT32)((x)*(1 << SCALE_FACTOR_BIC_FIXPOINT_SHIFT)))

#define SCALE_FACTOR_DEFAULT_FIXPOINT_SHIFT    12  /*NONE SCALE,vsd_bil*/
#define SCALE_FACTOR_VSDBIL_FIXPOINT_SHIFT     12  /*VER SCALE DOWN BIL*/

/*****************************************************************************************************/

/*#define GET_SCALE_FACTOR_BILI(src, dst) ((((src) - 1) << SCALE_FACTOR_BILI_FIXPOINT_SHIFT) / ((dst) - 1))*/
/*#define GET_SCALE_FACTOR_BIC(src, dst)  ((((src) - 1) << SCALE_FACTOR_BIC_FIXPOINT_SHIFT) / ((dst) - 1))*/
/*modified by hpz*/
#define GET_SCALE_FACTOR_BILI_DN(src, dst)  ((((src)*2 - 3) << (SCALE_FACTOR_BILI_DN_FIXPOINT_SHIFT-1)) / ((dst) - 1))
#define GET_SCALE_FACTOR_BILI_UP(src, dst)  ((((src)*2 - 3) << (SCALE_FACTOR_BILI_UP_FIXPOINT_SHIFT-1)) / ((dst) - 1))
#define GET_SCALE_FACTOR_BIC(src, dst)      ((((src)*2 - 3) << (SCALE_FACTOR_BIC_FIXPOINT_SHIFT-1)) / ((dst) - 1))

#define get_fimd_context(dev)	platform_get_drvdata(to_platform_device(dev))

static struct rk_fb_trsm_ops *trsm_lvds_ops;
static struct rk_fb_trsm_ops *trsm_edp_ops;
static struct rk_fb_trsm_ops *trsm_mipi_ops;

int rk_fb_trsm_ops_register(struct rk_fb_trsm_ops *ops, int type)
{
	switch (type) {
	case SCREEN_RGB:
	case SCREEN_LVDS:
	case SCREEN_DUAL_LVDS:
		trsm_lvds_ops = ops;
		break;
	case SCREEN_EDP:
		trsm_edp_ops = ops;
		break;
	case SCREEN_MIPI:
	case SCREEN_DUAL_MIPI:
		trsm_mipi_ops = ops;
		break;
	default:
		printk(KERN_WARNING "%s:un supported transmitter:%d!\n",
			__func__, type);
		break;
	}
	return 0;
}
struct fimd_driver_data {
	unsigned int timing_base;
};

static struct fimd_driver_data rockchip4_fimd_driver_data = {
	.timing_base = 0x0,
};

static struct fimd_driver_data rockchip5_fimd_driver_data = {
	.timing_base = 0x20000,
};

struct fimd_win_data {
	unsigned int		offset_x;
	unsigned int		offset_y;
	unsigned int		ovl_width;
	unsigned int		ovl_height;
	unsigned int		fb_width;
	unsigned int		fb_height;
	unsigned int		bpp;
	dma_addr_t		dma_addr;
	unsigned int		buf_offsize;
	unsigned int		line_size;	/* bytes */
	bool			enabled;
	bool			resume;
};

struct fimd_context {
	struct rockchip_drm_subdrv	subdrv;
	int				irq;
	struct drm_crtc			*crtc;
	struct clk			*pd;				//lcdc power domain
	struct clk			*hclk;				//lcdc AHP clk
	struct clk			*dclk;				//lcdc dclk
	struct clk			*aclk;				//lcdc share memory frequency
	void __iomem			*regs;
	struct fimd_win_data		win_data[WINDOWS_NR];
	unsigned int			clkdiv;
	unsigned int			default_win;
	unsigned long			irq_flags;
	u32				vidcon0;
	u32				vidcon1;
	int 				lcdc_id;
	bool				suspended;
	struct mutex			lock;
	wait_queue_head_t		wait_vsync_queue;
	atomic_t			wait_vsync_event;

	int clkon;
	void *regsbak;			//back up reg
	struct rockchip_drm_panel_info *panel;
	struct rk_screen *screen;
};

#include "rk3288_drm_fimd.h"
static int rk3288_lcdc_get_id(u32 phy_base)
{
	if (cpu_is_rk3288()) {
		if (phy_base == 0xff930000)/*vop big*/
			return 0;
		else if (phy_base == 0xff940000)/*vop lit*/	
			return 1;
		else
			return -EINVAL;
	} else {
		pr_err("un supported platform \n");
		return -EINVAL;
	}
}


static bool fimd_display_is_connected(struct device *dev)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO. */

	return true;
}

static void *fimd_get_panel(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	return ctx->panel;
}

static int fimd_check_timing(struct device *dev, void *timing)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO. */

	return 0;
}

static int fimd_display_power_on(struct device *dev, int mode)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO */

	return 0;
}

static struct rockchip_drm_display_ops fimd_display_ops = {
	.type = ROCKCHIP_DISPLAY_TYPE_LCD,
	.is_connected = fimd_display_is_connected,
	.get_panel = fimd_get_panel,
	.check_timing = fimd_check_timing,
	.power_on = fimd_display_power_on,
};

static void fimd_dpms(struct device *subdrv_dev, int mode)
{
	struct fimd_context *ctx = get_fimd_context(subdrv_dev);

	DRM_DEBUG_KMS("%s, %d\n", __FILE__, mode);

	mutex_lock(&ctx->lock);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		/*
		 * enable fimd hardware only if suspended status.
		 *
		 * P.S. fimd_dpms function would be called at booting time so
		 * clk_enable could be called double time.
		 */

		if(trsm_lvds_ops != NULL){
			printk(KERN_ERR"------>yzq enable lvds\n");	
			trsm_lvds_ops->enable();
		}
		if (ctx->suspended)
			pm_runtime_get_sync(subdrv_dev);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (!ctx->suspended)
			pm_runtime_put_sync(subdrv_dev);
		break;
	default:
		DRM_DEBUG_KMS("unspecified mode %d\n", mode);
		break;
	}

	mutex_unlock(&ctx->lock);
}

static void fimd_apply(struct device *subdrv_dev)
{
	struct fimd_context *ctx = get_fimd_context(subdrv_dev);
	struct rockchip_drm_manager *mgr = ctx->subdrv.manager;
	struct rockchip_drm_manager_ops *mgr_ops = mgr->ops;
	struct rockchip_drm_overlay_ops *ovl_ops = mgr->overlay_ops;
	struct fimd_win_data *win_data;
	int i;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		if (win_data->enabled && (ovl_ops && ovl_ops->commit))
			ovl_ops->commit(subdrv_dev, i);
	}

	if (mgr_ops && mgr_ops->commit)
		mgr_ops->commit(subdrv_dev);
}

static int rk3288_lcdc_alpha_cfg(struct fimd_context *ctx,int win_id)
{
	struct alpha_config alpha_config;
	struct fimd_win_data *win_data;
	enum alpha_mode alpha_mode;
	u32 mask, val;
	int ppixel_alpha,global_alpha;
	u32 src_alpha_ctl,dst_alpha_ctl;
	int g_alpha_val=0;

	win_data = &ctx->win_data[win_id];
	ppixel_alpha = (win_data->bpp==32) ? 1 : 0;
	global_alpha = 1; 
	alpha_config.src_global_alpha_val = 1;
	alpha_mode = AB_SRC_OVER;
	global_alpha = (g_alpha_val == 0) ? 0 : 1; 
	alpha_config.src_global_alpha_val = g_alpha_val;
	/*printk("%s,alpha_mode=%d,alpha_en=%d,ppixel_a=%d,gla_a=%d\n",
		__func__,win->alpha_mode,win->alpha_en,ppixel_alpha,global_alpha);*/
	switch(alpha_mode){
	case AB_USER_DEFINE:
		break;
 	case AB_CLEAR:
		alpha_config.src_factor_mode=AA_ZERO;
		alpha_config.dst_factor_mode=AA_ZERO;		
		break;
 	case AB_SRC:
		alpha_config.src_factor_mode=AA_ONE;
		alpha_config.dst_factor_mode=AA_ZERO;
		break;
 	case AB_DST:
		alpha_config.src_factor_mode=AA_ZERO;
		alpha_config.dst_factor_mode=AA_ONE;
		break;
 	case AB_SRC_OVER:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_ONE;
		alpha_config.dst_factor_mode=AA_SRC_INVERSE;		
		break;
 	case AB_DST_OVER:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC_INVERSE;
		alpha_config.dst_factor_mode=AA_ONE;
		break;
 	case AB_SRC_IN:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC;
		alpha_config.dst_factor_mode=AA_ZERO;
		break;
 	case AB_DST_IN:
		alpha_config.src_factor_mode=AA_ZERO;
		alpha_config.dst_factor_mode=AA_SRC;
		break;
 	case AB_SRC_OUT:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC_INVERSE;
		alpha_config.dst_factor_mode=AA_ZERO;		
		break;
 	case AB_DST_OUT:
		alpha_config.src_factor_mode=AA_ZERO;
		alpha_config.dst_factor_mode=AA_SRC_INVERSE;	
		break;
 	case AB_SRC_ATOP:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC;
		alpha_config.dst_factor_mode=AA_SRC_INVERSE;		
		break;
 	case AB_DST_ATOP:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC_INVERSE;
		alpha_config.dst_factor_mode=AA_SRC;		
		break;
 	case XOR:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC_INVERSE;
		alpha_config.dst_factor_mode=AA_SRC_INVERSE;			
		break;	
 	case AB_SRC_OVER_GLOBAL:	
		alpha_config.src_global_alpha_mode=AA_PER_PIX_GLOBAL;
		alpha_config.src_color_mode=AA_SRC_NO_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC_GLOBAL;
		alpha_config.dst_factor_mode=AA_SRC_INVERSE;
		break;
	default:
	    	pr_err("alpha mode error\n");
      		break;		
	}
	if((ppixel_alpha == 1)&&(global_alpha == 1)){
		alpha_config.src_global_alpha_mode = AA_PER_PIX_GLOBAL;
	}else if(ppixel_alpha == 1){
		alpha_config.src_global_alpha_mode = AA_PER_PIX;
	}else if(global_alpha == 1){
		alpha_config.src_global_alpha_mode = AA_GLOBAL;
	}else{
		pr_err("alpha_en should be 0\n");
	}
	alpha_config.src_alpha_mode = AA_STRAIGHT;
	alpha_config.src_alpha_cal_m0 = AA_NO_SAT;

	switch(win_id){
	case 0:
		src_alpha_ctl = 0x60;
		dst_alpha_ctl = 0x64;
		break;
	case 1:
		src_alpha_ctl = 0xa0;
		dst_alpha_ctl = 0xa4;
		break;
	case 2:
		src_alpha_ctl = 0xdc;
		dst_alpha_ctl = 0xec;
		break;
	case 3:
		src_alpha_ctl = 0x12c;
		dst_alpha_ctl = 0x13c;
		break;
	}
	mask = m_WIN0_DST_FACTOR_M0;
	val  = v_WIN0_DST_FACTOR_M0(alpha_config.dst_factor_mode);
	lcdc_msk_reg(ctx, dst_alpha_ctl, mask, val);
	mask = m_WIN0_SRC_ALPHA_EN | m_WIN0_SRC_COLOR_M0 |
		m_WIN0_SRC_ALPHA_M0 | m_WIN0_SRC_BLEND_M0 |
		m_WIN0_SRC_ALPHA_CAL_M0 | m_WIN0_SRC_FACTOR_M0|
		m_WIN0_SRC_GLOBAL_ALPHA;
	val = v_WIN0_SRC_ALPHA_EN(1) | 
		v_WIN0_SRC_COLOR_M0(alpha_config.src_color_mode) |
		v_WIN0_SRC_ALPHA_M0(alpha_config.src_alpha_mode) |
		v_WIN0_SRC_BLEND_M0(alpha_config.src_global_alpha_mode) |
		v_WIN0_SRC_ALPHA_CAL_M0(alpha_config.src_alpha_cal_m0) |
		v_WIN0_SRC_FACTOR_M0(alpha_config.src_factor_mode) |
		v_WIN0_SRC_GLOBAL_ALPHA(alpha_config.src_global_alpha_val);
	lcdc_msk_reg(ctx, src_alpha_ctl, mask, val);

	return 0;
}
static int rk3288_win_0_1_reg_update(struct fimd_context *ctx,int win_id)
{
	struct fimd_win_data *win_data;
	unsigned int mask, val, off;
	struct rk_screen *screen = ctx->screen;
	u8 fmt_cfg = 0;
	u32 xpos, ypos;
	off = win_id * 0x40;
	win_data = &ctx->win_data[win_id];
	switch(win_data->bpp){
		case 32:
			fmt_cfg = 0;
			break;
		case 24:
			fmt_cfg = 1;
			break;
		case 16:
			fmt_cfg = 2;
			break;
		default:
			printk("not support format %d\n",win_data->bpp);
			break;
	}

	xpos = win_data->offset_x + screen->mode.left_margin + screen->mode.hsync_len;
	ypos = win_data->offset_y + screen->mode.upper_margin + screen->mode.vsync_len;
	mask =  m_WIN0_EN | m_WIN0_DATA_FMT ;
	val  =  v_WIN0_EN(1) | v_WIN0_DATA_FMT(fmt_cfg);
	lcdc_msk_reg(ctx, WIN0_CTRL0+off, mask,val);	

	val =	v_WIN0_VIR_STRIDE(win_data->fb_width);
	lcdc_writel(ctx, WIN0_VIR+off, val);	
	val =	v_WIN0_ACT_WIDTH(win_data->fb_width) |
		v_WIN0_ACT_HEIGHT(win_data->fb_height);
	lcdc_writel(ctx, WIN0_ACT_INFO+off, val); 

	val =	v_WIN0_DSP_WIDTH(win_data->ovl_width) |
		v_WIN0_DSP_HEIGHT(win_data->ovl_height);
	lcdc_writel(ctx, WIN0_DSP_INFO+off, val); 

	val =	v_WIN0_DSP_XST(xpos) |
		v_WIN0_DSP_YST(ypos);
	lcdc_writel(ctx, WIN0_DSP_ST+off, val); 
	lcdc_writel(ctx, WIN0_YRGB_MST+off, win_data->dma_addr );

	if(win_id == 1)
		rk3288_lcdc_alpha_cfg(ctx,win_id);
	lcdc_cfg_done(ctx);
	return 0;
}

static int rk3288_lcdc_post_cfg(struct fimd_context *ctx)
{
	struct rk_screen *screen = ctx->screen;
	u16 x_res = screen->mode.xres;
	u16 y_res = screen->mode.yres;
	u32 mask, val;
	u16 h_total,v_total;
	u16 post_hsd_en,post_vsd_en;
	u16 post_dsp_hact_st,post_dsp_hact_end;	
	u16 post_dsp_vact_st,post_dsp_vact_end;
	u16 post_dsp_vact_st_f1,post_dsp_vact_end_f1;
	u16 post_h_fac,post_v_fac;

	h_total = screen->mode.hsync_len+screen->mode.left_margin +
		  x_res + screen->mode.right_margin;
	v_total = screen->mode.vsync_len+screen->mode.upper_margin +
		  y_res + screen->mode.lower_margin;

	if(screen->post_dsp_stx + screen->post_xsize > x_res){		
		printk(KERN_ERR"post:stx[%d] + xsize[%d] > x_res[%d]\n",
			screen->post_dsp_stx,screen->post_xsize,x_res);
		screen->post_dsp_stx = x_res - screen->post_xsize;
	}
	if(screen->x_mirror == 0){
		post_dsp_hact_st=screen->post_dsp_stx + 
			screen->mode.hsync_len+screen->mode.left_margin;
		post_dsp_hact_end = post_dsp_hact_st + screen->post_xsize;
	}else{
		post_dsp_hact_end = h_total - screen->mode.right_margin -
					screen->post_dsp_stx;
		post_dsp_hact_st = post_dsp_hact_end - screen->post_xsize;
	}	
	if((screen->post_xsize < x_res)&&(screen->post_xsize != 0)){
		post_hsd_en = 1;
		post_h_fac = 
			GET_SCALE_FACTOR_BILI_DN(x_res , screen->post_xsize); 
	}else{
		post_hsd_en = 0;
		post_h_fac = 0x1000;
	}


	if(screen->post_dsp_sty + screen->post_ysize > y_res){
		printk(KERN_ERR "post:sty[%d] + ysize[%d] > y_res[%d]\n",
			screen->post_dsp_sty,screen->post_ysize,y_res);
		screen->post_dsp_sty = y_res - screen->post_ysize;	
	}
	
	if(screen->y_mirror == 0){
		post_dsp_vact_st = screen->post_dsp_sty + 
			screen->mode.vsync_len+screen->mode.upper_margin;
		post_dsp_vact_end = post_dsp_vact_st + screen->post_ysize;
	}else{
		post_dsp_vact_end = v_total - screen->mode.lower_margin -
					- screen->post_dsp_sty;
		post_dsp_hact_st = post_dsp_vact_end - screen->post_ysize;
	}
	if((screen->post_ysize < y_res)&&(screen->post_ysize != 0)){
		post_vsd_en = 1;
		post_v_fac = GET_SCALE_FACTOR_BILI_DN(y_res, screen->post_ysize);		
	}else{
		post_vsd_en = 0;
		post_v_fac = 0x1000;
	}

	if(screen->interlace == 1){
		post_dsp_vact_st_f1  = v_total + post_dsp_vact_st;
		post_dsp_vact_end_f1 = post_dsp_vact_st_f1 + screen->post_ysize;
	}else{
		post_dsp_vact_st_f1  = 0;
		post_dsp_vact_end_f1 = 0;
	}
	printk(KERN_ERR"post:xsize=%d,ysize=%d,xpos=%d,ypos=%d,"
	      "hsd_en=%d,h_fac=%d,vsd_en=%d,v_fac=%d\n",
		screen->post_xsize,screen->post_ysize,screen->xpos,screen->ypos,
		post_hsd_en,post_h_fac,post_vsd_en,post_v_fac);
	mask = m_DSP_HACT_END_POST | m_DSP_HACT_ST_POST;
	val = v_DSP_HACT_END_POST(post_dsp_hact_end) | 
	      v_DSP_HACT_ST_POST(post_dsp_hact_st);
	lcdc_msk_reg(ctx, POST_DSP_HACT_INFO, mask, val);

	mask = m_DSP_VACT_END_POST | m_DSP_VACT_ST_POST;
	val = v_DSP_VACT_END_POST(post_dsp_vact_end) | 
	      v_DSP_VACT_ST_POST(post_dsp_vact_st);
	lcdc_msk_reg(ctx, POST_DSP_VACT_INFO, mask, val);

	mask = m_POST_HS_FACTOR_YRGB | m_POST_VS_FACTOR_YRGB;
	val = v_POST_HS_FACTOR_YRGB(post_h_fac) |
		v_POST_VS_FACTOR_YRGB(post_v_fac);
	lcdc_msk_reg(ctx, POST_SCL_FACTOR_YRGB, mask, val);

	mask = m_DSP_VACT_END_POST_F1 | m_DSP_VACT_ST_POST_F1;
	val = v_DSP_VACT_END_POST_F1(post_dsp_vact_end_f1) |
		v_DSP_VACT_ST_POST_F1(post_dsp_vact_st_f1);
	lcdc_msk_reg(ctx, POST_DSP_VACT_INFO_F1, mask, val);

	mask = m_POST_HOR_SD_EN | m_POST_VER_SD_EN;
	val = v_POST_HOR_SD_EN(post_hsd_en) | v_POST_VER_SD_EN(post_vsd_en);
	lcdc_msk_reg(ctx, POST_SCL_CTRL, mask, val);
	return 0;
}
static void fimd_commit(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct rockchip_drm_panel_info *panel = ctx->panel;
	struct rk_screen *screen = ctx->screen;
	u16 hsync_len = screen->mode.hsync_len;
	u16 left_margin = screen->mode.left_margin;
	u16 right_margin = screen->mode.right_margin;
	u16 vsync_len = screen->mode.vsync_len;
	u16 upper_margin = screen->mode.upper_margin;
	u16 lower_margin = screen->mode.lower_margin;
	u16 x_res = screen->mode.xres;
	u16 y_res = screen->mode.yres;
	u32 mask, val;
	int face;
	u32 v=0;
	u16 h_total,v_total;
	h_total = hsync_len + left_margin  + x_res + right_margin;
	v_total = vsync_len + upper_margin + y_res + lower_margin;
	screen->post_dsp_stx=0;
	screen->post_dsp_sty=0;
	screen->post_xsize =x_res;
	screen->post_ysize = y_res;

	printk(KERN_ERR"%s %d\n", __func__,__LINE__);
	if (ctx->suspended)
		return;

	printk(KERN_ERR"%s %d\n", __func__,__LINE__);
	if(!ctx->clkon)
		return;
#if 1
	switch (screen->face) {
		case OUT_P565:
			face = OUT_P565;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0) |
				v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(ctx, DSP_CTRL1, mask, val);
			break;
		case OUT_P666:
			face = OUT_P666;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1) |
				v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(ctx, DSP_CTRL1, mask, val);
			break;
		case OUT_D888_P565:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0) |
				v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(ctx, DSP_CTRL1, mask, val);
			break;
		case OUT_D888_P666:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1) |
				v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(ctx, DSP_CTRL1, mask, val);
			break;
		case OUT_P888:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_UP_EN;
			val = v_DITHER_DOWN_EN(0) | v_DITHER_UP_EN(0);
			lcdc_msk_reg(ctx, DSP_CTRL1, mask, val);
			break;
		default:
			printk("un supported interface!\n");
			break;
	}
	switch(screen->type){
		case SCREEN_RGB:
		case SCREEN_LVDS:
		case SCREEN_DUAL_LVDS:			
			mask = m_RGB_OUT_EN;
			val = v_RGB_OUT_EN(1);
			v = 1 << (3+16);
			v |= (ctx->lcdc_id << 3);
			break;
		case SCREEN_HDMI:
			mask = m_HDMI_OUT_EN;
			val = v_HDMI_OUT_EN(1);
			/*v = 1 << (4+16);
			  v |= (ctx->id << 4);*/	
			break;
		case SCREEN_MIPI:
			mask = m_MIPI_OUT_EN;
			val = v_MIPI_OUT_EN(1);
			/*v = (1 << (6+16))||(1 << (9+16));
			  v |= (ctx->id << 6);
			  v |= (ctx->id << 9);*/		
			break;
		case SCREEN_DUAL_MIPI:
			mask = m_MIPI_OUT_EN | m_DOUB_CHANNEL_EN;
			val = v_MIPI_OUT_EN(1) | v_DOUB_CHANNEL_EN(1);
			/*v = (1 << (6+16))||(1 << (9+16));
			  v |= (ctx->id << 6);
			  v |= (ctx->id << 9);*/	
			break;
		case SCREEN_EDP:
			face = OUT_RGB_AAA;  /*RGB AAA output*/
			mask = m_DITHER_DOWN_EN | m_DITHER_UP_EN;
			val = v_DITHER_DOWN_EN(0) | v_DITHER_UP_EN(0);
			lcdc_msk_reg(ctx, DSP_CTRL1, mask, val);
			mask = m_EDP_OUT_EN;
			val = v_EDP_OUT_EN(1);
			/*v = 1 << (5+16);
			  v |= (ctx->id << 5);*/		
			break;
	}
	lcdc_msk_reg(ctx, SYS_CTRL, mask, val);
#ifdef USE_ION_MMU
	mask = m_MMU_EN;
	val = v_MMU_EN(1);
	lcdc_msk_reg(ctx, SYS_CTRL, mask, val);
	mask = m_AXI_MAX_OUTSTANDING_EN | m_AXI_OUTSTANDING_MAX_NUM;
	val = v_AXI_OUTSTANDING_MAX_NUM(31) | v_AXI_MAX_OUTSTANDING_EN(1);
	lcdc_msk_reg(ctx, SYS_CTRL1, mask, val);		
#endif	
	writel_relaxed(v, RK_GRF_VIRT + RK3288_GRF_SOC_CON6);
	mask = m_DSP_OUT_MODE | m_DSP_HSYNC_POL | m_DSP_VSYNC_POL |
		m_DSP_DEN_POL | m_DSP_DCLK_POL | m_DSP_BG_SWAP | 
		m_DSP_RB_SWAP | m_DSP_RG_SWAP | m_DSP_DELTA_SWAP |
		m_DSP_DUMMY_SWAP | m_DSP_OUT_ZERO | m_DSP_BLANK_EN | 
		m_DSP_BLACK_EN | m_DSP_X_MIR_EN | m_DSP_Y_MIR_EN;
	val = v_DSP_OUT_MODE(face) | v_DSP_HSYNC_POL(screen->pin_hsync) |
		v_DSP_VSYNC_POL(screen->pin_vsync) | 
		v_DSP_DEN_POL(screen->pin_den) | v_DSP_DCLK_POL(screen->pin_dclk) |
		v_DSP_BG_SWAP(screen->swap_gb) | v_DSP_RB_SWAP(screen->swap_rb) | 
		v_DSP_RG_SWAP(screen->swap_rg) | 
		v_DSP_DELTA_SWAP(screen->swap_delta) |
		v_DSP_DUMMY_SWAP(screen->swap_dumy) | v_DSP_OUT_ZERO(0) | 
		v_DSP_BLANK_EN(0) | v_DSP_BLACK_EN(0) |
		v_DSP_X_MIR_EN(screen->x_mirror) | v_DSP_Y_MIR_EN(screen->y_mirror);
	lcdc_msk_reg(ctx, DSP_CTRL0, mask, val);

	mask = m_DSP_BG_BLUE | m_DSP_BG_GREEN | m_DSP_BG_RED;
	val  = v_DSP_BG_BLUE(0x3ff) | v_DSP_BG_GREEN(0) | v_DSP_BG_RED(0);
	lcdc_msk_reg(ctx, DSP_BG, mask, val);

	mask = m_DSP_HS_PW | m_DSP_HTOTAL;
	val = v_DSP_HS_PW(hsync_len) | v_DSP_HTOTAL(h_total);
	lcdc_msk_reg(ctx, DSP_HTOTAL_HS_END, mask, val);

	mask = m_DSP_HACT_END | m_DSP_HACT_ST;
	val = v_DSP_HACT_END(hsync_len + left_margin + x_res) |
		v_DSP_HACT_ST(hsync_len + left_margin);
	lcdc_msk_reg(ctx, DSP_HACT_ST_END, mask, val);

	mask = m_DSP_VS_PW | m_DSP_VTOTAL;
	val = v_DSP_VS_PW(vsync_len) | v_DSP_VTOTAL(v_total);
	lcdc_msk_reg(ctx, DSP_VTOTAL_VS_END, mask, val);

	mask = m_DSP_VACT_END | m_DSP_VACT_ST;
	val = v_DSP_VACT_END(vsync_len + upper_margin + y_res) |
		v_DSP_VACT_ST(vsync_len + upper_margin);
	lcdc_msk_reg(ctx, DSP_VACT_ST_END, mask, val);

	rk3288_lcdc_post_cfg(ctx);
#endif
}

static int fimd_enable_vblank(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	u32 val,mask;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (ctx->suspended)
		return -EPERM;

	if (!test_and_set_bit(0, &ctx->irq_flags)) {
		mask = m_FS_INTR_CLR | m_FS_INTR_EN | m_LINE_FLAG_INTR_CLR |
		    m_LINE_FLAG_INTR_EN | m_BUS_ERROR_INTR_CLR | 
		    m_BUS_ERROR_INTR_EN | m_DSP_LINE_FLAG_NUM;
		val = v_FS_INTR_CLR(1) | v_FS_INTR_EN(1) | v_LINE_FLAG_INTR_CLR(0) |
		    v_LINE_FLAG_INTR_EN(0) | v_BUS_ERROR_INTR_CLR(0) | v_BUS_ERROR_INTR_EN(0) ;
		lcdc_msk_reg(ctx, INTR_CTRL0, mask, val);
		lcdc_cfg_done(ctx);
	}

	return 0;
}

static void fimd_disable_vblank(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	u32 val,mask;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (ctx->suspended)
		return;

	if (test_and_clear_bit(0, &ctx->irq_flags)) {
		mask = m_DSP_HOLD_VALID_INTR_EN | m_FS_INTR_EN |
			m_LINE_FLAG_INTR_EN | m_BUS_ERROR_INTR_EN;
		val = v_DSP_HOLD_VALID_INTR_EN(0) | v_FS_INTR_EN(0) |
			v_LINE_FLAG_INTR_EN(0) | v_BUS_ERROR_INTR_EN(0);
		lcdc_msk_reg(ctx, INTR_CTRL0, mask, val);

		mask = m_DSP_HOLD_VALID_INTR_CLR | m_FS_INTR_CLR |
			m_LINE_FLAG_INTR_CLR | m_LINE_FLAG_INTR_CLR;
		val = v_DSP_HOLD_VALID_INTR_CLR(0) | v_FS_INTR_CLR(0) |
			v_LINE_FLAG_INTR_CLR(0) | v_BUS_ERROR_INTR_CLR(0);
		lcdc_msk_reg(ctx, INTR_CTRL0, mask, val);

		mask = m_WIN0_EMPTY_INTR_EN | m_WIN1_EMPTY_INTR_EN |
			m_WIN2_EMPTY_INTR_EN | m_WIN3_EMPTY_INTR_EN |
			m_HWC_EMPTY_INTR_EN | m_POST_BUF_EMPTY_INTR_EN |
			m_POST_BUF_EMPTY_INTR_EN;
		val = v_WIN0_EMPTY_INTR_EN(0) | v_WIN1_EMPTY_INTR_EN(0) |
			v_WIN2_EMPTY_INTR_EN(0) | v_WIN3_EMPTY_INTR_EN(0) |
			v_HWC_EMPTY_INTR_EN(0) | v_POST_BUF_EMPTY_INTR_EN(0) |
			v_PWM_GEN_INTR_EN(0);
		lcdc_msk_reg(ctx, INTR_CTRL1, mask, val);

		mask = m_WIN0_EMPTY_INTR_CLR | m_WIN1_EMPTY_INTR_CLR |
			m_WIN2_EMPTY_INTR_CLR | m_WIN3_EMPTY_INTR_CLR |
			m_HWC_EMPTY_INTR_CLR | m_POST_BUF_EMPTY_INTR_CLR |
			m_POST_BUF_EMPTY_INTR_CLR;
		val = v_WIN0_EMPTY_INTR_CLR(0) | v_WIN1_EMPTY_INTR_CLR(0) |
			v_WIN2_EMPTY_INTR_CLR(0) | v_WIN3_EMPTY_INTR_CLR(0) |
			v_HWC_EMPTY_INTR_CLR(0) | v_POST_BUF_EMPTY_INTR_CLR(0) |
			v_PWM_GEN_INTR_CLR(0);
		lcdc_msk_reg(ctx, INTR_CTRL1, mask, val);		
		lcdc_cfg_done(ctx);


	}
}

static void fimd_wait_for_vblank(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	if (ctx->suspended)
		return;

	atomic_set(&ctx->wait_vsync_event, 1);

	/*
	 * wait for FIMD to signal VSYNC interrupt or return after
	 * timeout which is set to 50ms (refresh rate of 20).
	 */
	if (!wait_event_timeout(ctx->wait_vsync_queue,
				!atomic_read(&ctx->wait_vsync_event),
				DRM_HZ/20))
		DRM_DEBUG_KMS("vblank wait timed out.\n");
}

static struct rockchip_drm_manager_ops fimd_manager_ops = {
	.dpms = fimd_dpms,
	.apply = fimd_apply,
	.commit = fimd_commit,
	.enable_vblank = fimd_enable_vblank,
	.disable_vblank = fimd_disable_vblank,
	.wait_for_vblank = fimd_wait_for_vblank,
};

static void fimd_win_mode_set(struct device *dev,
			      struct rockchip_drm_overlay *overlay)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	int win;
	unsigned long offset;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (!overlay) {
		dev_err(dev, "overlay is NULL\n");
		return;
	}

	win = overlay->zpos;
	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
		return;

	offset = overlay->fb_x * (overlay->bpp >> 3);
	offset += overlay->fb_y * overlay->pitch;

	DRM_DEBUG_KMS("offset = 0x%lx, pitch = %x\n", offset, overlay->pitch);

	win_data = &ctx->win_data[win];

	win_data->offset_x = overlay->crtc_x;
	win_data->offset_y = overlay->crtc_y;
	win_data->ovl_width = overlay->crtc_width;
	win_data->ovl_height = overlay->crtc_height;
	win_data->fb_width = overlay->fb_width;
	win_data->fb_height = overlay->fb_height;
	win_data->dma_addr = overlay->dma_addr[0] + offset;
	win_data->bpp = overlay->bpp;
	win_data->buf_offsize = (overlay->fb_width - overlay->crtc_width) *
				(overlay->bpp >> 3);
	win_data->line_size = overlay->crtc_width * (overlay->bpp >> 3);

	DRM_DEBUG_KMS("offset_x = %d, offset_y = %d\n",
			win_data->offset_x, win_data->offset_y);
	DRM_DEBUG_KMS("ovl_width = %d, ovl_height = %d\n",
			win_data->ovl_width, win_data->ovl_height);
	DRM_DEBUG_KMS("paddr = 0x%lx\n", (unsigned long)win_data->dma_addr);
	DRM_DEBUG_KMS("fb_width = %d, crtc_width = %d\n",
			overlay->fb_width, overlay->crtc_width);
}

static void fimd_win_set_pixfmt(struct device *dev, unsigned int win)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data = &ctx->win_data[win];
	u8 fmt_cfg = 0;

#if 0
	DRM_DEBUG_KMS("%s\n", __FILE__);
	switch(win_data->bpp){
		case 32:
			fmt_cfg = 0;
			break;
		case 24:
			fmt_cfg = 1;
			break;
		case 16:
			fmt_cfg = 2;
			break;
		default:
			printk("not support format %d\n",win_data->bpp);
			break;
	}


	printk(KERN_ERR"------>yzq %d  SYS_CTRL=%x \n",__LINE__,lcdc_readl(ctx,SYS_CTRL));
	lcdc_msk_reg(ctx , SYS_CTRL, m_WIN0_FORMAT, v_WIN0_FORMAT(fmt_cfg));
	printk(KERN_ERR"------>yzq %d  SYS_CTRL=%x \n",__LINE__,lcdc_readl(ctx,SYS_CTRL));
#endif
}

static void fimd_win_set_colkey(struct device *dev, unsigned int win)
{
//	struct fimd_context *ctx = get_fimd_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

}

static void fimd_win_commit(struct device *dev, int zpos)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	struct rk_screen *screen = ctx->screen;
	int win = zpos;
	unsigned long val,  size;
	u32 xpos, ypos;

//	printk(KERN_ERR"%s %d\n", __func__,__LINE__);

	if (ctx->suspended)
		return;

	if (!ctx->clkon)
		return;
//	printk(KERN_ERR"%s %d\n", __func__,__LINE__);
	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
		return;

	win_data = &ctx->win_data[win];
	switch(win){
		case 0:
			rk3288_win_0_1_reg_update(ctx,0);
			break;
		case 1:
//	printk(KERN_ERR"-->yzq dma_addr=%x buf_offsize=%x win_data->fb_width=%d \nwin_data->fb_height=%d win_data->ovl_height=%d  win_data->ovl_width=%d \n win_data->offset_x=%d win_data->offset_y=%d win_data->line_size=%d\n win_data->bpp=%d ",win_data->dma_addr,win_data->buf_offsize,win_data->fb_width,win_data->fb_height,win_data->ovl_height, win_data->ovl_width,win_data->offset_x,win_data->offset_y,win_data->line_size,win_data->bpp);
			rk3288_win_0_1_reg_update(ctx,1);
			break;
		case 2:
			printk("----->yzq not support now win_id=%d\n",win);
		//	rk3288_win_2_3_reg_update(ctx,2);
			break;
		case 3:
			printk("----->yzq not support now win_id=%d\n",win);
		//	rk3288_win_2_3_reg_update(ctx,3);
			break;
		default:
			printk("----->yzq not support now win_id=%d\n",win);
			break;
	}
//	printk("----->yzq now win_id=%d\n",win);

	//rk3288_lcdc_post_cfg(screen);
	win_data->enabled = true;

}

static void fimd_win_disable(struct device *dev, int zpos)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	int win = zpos;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
		return;

	win_data = &ctx->win_data[win];

	if (ctx->suspended) {
		/* do not resume this window*/
		win_data->resume = false;
		return;
	}

	win_data->enabled = false;
}

static struct rockchip_drm_overlay_ops fimd_overlay_ops = {
	.mode_set = fimd_win_mode_set,
	.commit = fimd_win_commit,
	.disable = fimd_win_disable,
};

static struct rockchip_drm_manager fimd_manager = {
	.pipe		= -1,
	.ops		= &fimd_manager_ops,
	.overlay_ops	= &fimd_overlay_ops,
	.display_ops	= &fimd_display_ops,
};

static irqreturn_t fimd_irq_handler(int irq, void *dev_id)
{
	struct fimd_context *ctx = (struct fimd_context *)dev_id;
	struct rockchip_drm_subdrv *subdrv = &ctx->subdrv;
	struct drm_device *drm_dev = subdrv->drm_dev;
	struct rockchip_drm_manager *manager = subdrv->manager;
	u32 intr0_reg;

	intr0_reg = lcdc_readl(ctx, INTR_CTRL0);

	if(intr0_reg & m_FS_INTR_STS){
		lcdc_msk_reg(ctx, INTR_CTRL0, m_FS_INTR_CLR,
			     v_FS_INTR_CLR(1));

	}

	/* check the crtc is detached already from encoder */
	if (manager->pipe < 0)
		goto out;

	drm_handle_vblank(drm_dev, manager->pipe);
	rockchip_drm_crtc_finish_pageflip(drm_dev, manager->pipe);

	/* set wait vsync event to zero and wake up queue. */
	if (atomic_read(&ctx->wait_vsync_event)) {
		atomic_set(&ctx->wait_vsync_event, 0);
		DRM_WAKEUP(&ctx->wait_vsync_queue);
	}
out:
	return IRQ_HANDLED;
}

static int fimd_subdrv_probe(struct drm_device *drm_dev, struct device *dev)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = 1, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *	just specific driver own one instead because
	 *	drm framework supports only one irq handler.
	 */
	drm_dev->irq_enabled = 1;

	/*
	 * with vblank_disable_allowed = 1, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	drm_dev->vblank_disable_allowed = 1;

	/* attach this sub driver to iommu mapping if supported. */
	if (is_drm_iommu_supported(drm_dev))
		drm_iommu_attach_device(drm_dev, dev);

	return 0;
}

static void fimd_subdrv_remove(struct drm_device *drm_dev, struct device *dev)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* detach this sub driver from iommu mapping if supported. */
	if (is_drm_iommu_supported(drm_dev))
		drm_iommu_detach_device(drm_dev, dev);
}


static void fimd_clear_win(struct fimd_context *ctx, int win)
{
	u32 val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

}

static int fimd_clock(struct fimd_context *ctx, bool enable)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);
printk(KERN_ERR"---->yzq %s %d \n",__func__,__LINE__);
	if (enable) {
		if(ctx->clkon)
			return 0;
		int ret;

		ret = clk_prepare_enable(ctx->hclk);
		if (ret < 0)
			return ret;

		ret = clk_prepare_enable(ctx->dclk);
		if (ret < 0)
			return ret;
		
		ret = clk_prepare_enable(ctx->aclk);
		if (ret < 0)
			return ret;
		ctx->clkon=1;
	} else {
		if(!ctx->clkon)
			return 0;
		clk_disable_unprepare(ctx->aclk);
		clk_disable_unprepare(ctx->dclk);
		clk_disable_unprepare(ctx->hclk);
		ctx->clkon=0;
	}

	return 0;
}

static void fimd_window_suspend(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		win_data->resume = win_data->enabled;
		fimd_win_disable(dev, i);
	}
	fimd_wait_for_vblank(dev);
}

static void fimd_window_resume(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		win_data->enabled = win_data->resume;
		win_data->resume = false;
	}
}

static int fimd_activate(struct fimd_context *ctx, bool enable)
{
	struct device *dev = ctx->subdrv.dev;
	if (enable) {
		int ret;

		ret = fimd_clock(ctx, true);
		if (ret < 0)
			return ret;

		ctx->suspended = false;

		/* if vblank was enabled status, enable it again. */
		if (test_and_clear_bit(0, &ctx->irq_flags))
			fimd_enable_vblank(dev);

		fimd_window_resume(dev);
	} else {
		fimd_window_suspend(dev);

		fimd_clock(ctx, false);
		ctx->suspended = true;
	}

	return 0;
}

int rk_fb_video_mode_from_timing(const struct display_timing *dt, 
				struct rk_screen *screen)
{
	screen->mode.pixclock = dt->pixelclock.typ;
	screen->mode.left_margin = dt->hback_porch.typ;
	screen->mode.right_margin = dt->hfront_porch.typ;
	screen->mode.xres = dt->hactive.typ;
	screen->mode.hsync_len = dt->hsync_len.typ;
	screen->mode.upper_margin = dt->vback_porch.typ;
	screen->mode.lower_margin = dt->vfront_porch.typ;
	screen->mode.yres = dt->vactive.typ;
	screen->mode.vsync_len = dt->vsync_len.typ;
	screen->type = SCREEN_LVDS;
	screen->lvds_format = LVDS_8BIT_2;
	screen->face = OUT_D888_P666;

	if (dt->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		screen->pin_dclk = 1;
	else
		screen->pin_dclk = 0;
	if(dt->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;
	if(dt->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;
	if(dt->flags & DISPLAY_FLAGS_DE_HIGH)
		screen->pin_den = 1;
	else
		screen->pin_den = 0;
	
	return 0;
	
}

int rk_fb_prase_timing_dt(struct device_node *np, struct rk_screen *screen)
{
	struct display_timings *disp_timing;
	struct display_timing *dt;
	disp_timing = of_get_display_timings(np);
	if (!disp_timing) {
		pr_err("parse display timing err\n");
		return -EINVAL;
	}
	dt = display_timings_get(disp_timing, 0);
	rk_fb_video_mode_from_timing(dt, screen);
	printk(KERN_ERR "dclk:%d\n"
			 "hactive:%d\n"
			 "hback_porch:%d\n"
			 "hfront_porch:%d\n"
			 "hsync_len:%d\n"
			 "vactive:%d\n"
			 "vback_porch:%d\n"
			 "vfront_porch:%d\n"
			 "vsync_len:%d\n"
			 "screen_type:%d\n"
			 "lvds_format:%d\n"
			 "face:%d\n",
			dt->pixelclock.typ,
			dt->hactive.typ,
			dt->hback_porch.typ,
			dt->hfront_porch.typ,
			dt->hsync_len.typ,
			dt->vactive.typ,
			dt->vback_porch.typ,
			dt->vfront_porch.typ,
			dt->vsync_len.typ,
			dt->screen_type,
			dt->lvds_format,
			dt->face);
	return 0;

}

static int fimd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_context *ctx;
	struct rockchip_drm_subdrv *subdrv;
	struct rockchip_drm_fimd_pdata *pdata;
	struct rockchip_drm_panel_info *panel;
	struct device_node *np = pdev->dev.of_node;
	struct rk_screen *screen;
	int prop;
	int reg_len;
	struct resource *res;
	int win;
	int val;
	int ret = -EINVAL;

	DRM_DEBUG_KMS("%s\n", __FILE__);
	of_property_read_u32(np, "rockchip,prop", &prop);
	if (prop == EXTEND) {
		printk("---->%s not support lcdc EXTEND now\n",__func__);
			return 0;
	}
	if (of_property_read_u32(np, "rockchip,pwr18", &val))
	{
		printk("----->%s default set it as 3.xv power supply",__func__);
	}
	else{
		if(val){
			printk("----->%s lcdc pwr is 1.8, not supply now",__func__);
		}else{
			printk("----->%s lcdc pwr is 3.3v",__func__);
		}
	}

	if (dev->of_node) {
		panel = devm_kzalloc(dev, sizeof(struct rockchip_drm_panel_info), GFP_KERNEL);
		screen = devm_kzalloc(dev, sizeof(struct rk_screen), GFP_KERNEL);
		rk_fb_get_prmry_screen(screen);
		memcpy(&panel->timing,&screen->mode,sizeof(struct fb_videomode)); 
	} else {
		DRM_ERROR("no platform data specified\n");
		return -EINVAL;
	}

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ctx->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->regs))
		return PTR_ERR(ctx->regs);
	reg_len = resource_size(res);
	ctx->regsbak = devm_kzalloc(dev,reg_len,GFP_KERNEL);
	ctx->lcdc_id = rk3288_lcdc_get_id(res->start);	
	ctx->screen = screen;
	ctx->hclk = devm_clk_get(dev, "hclk_lcdc");
	ctx->aclk = devm_clk_get(dev, "aclk_lcdc");
	ctx->dclk = devm_clk_get(dev, "dclk_lcdc");

	ctx->irq = platform_get_irq(pdev, 0);
	if (ctx->irq < 0) {
		dev_err(dev, "cannot find IRQ for lcdc%d\n",
			ctx->lcdc_id);
		return -ENXIO;
	}
	ret = devm_request_irq(dev, ctx->irq, fimd_irq_handler,
							0, "drm_fimd", ctx);
	if (ret) {
		dev_err(dev, "irq request failed.\n");
		return ret;
	}

	ctx->default_win = 0;// pdata->default_win;
	ctx->panel = panel;
	DRM_INIT_WAITQUEUE(&ctx->wait_vsync_queue);
	atomic_set(&ctx->wait_vsync_event, 0);

	subdrv = &ctx->subdrv;

	subdrv->dev = dev;
	subdrv->manager = &fimd_manager;
	subdrv->probe = fimd_subdrv_probe;
	subdrv->remove = fimd_subdrv_remove;

	mutex_init(&ctx->lock);

	platform_set_drvdata(pdev, ctx);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);
	
	ret = clk_set_rate(ctx->dclk, ctx->screen->mode.pixclock);
	if (ret)
		printk( "set lcdc%d dclk failed\n", ctx->lcdc_id);
	
	fimd_activate(ctx, true);

	if(trsm_lvds_ops != NULL){
		printk(KERN_ERR"------>yzq enable lvds\n");	
		trsm_lvds_ops->enable();
	}
	memcpy(ctx->regsbak,ctx->regs,reg_len);
	rockchip_drm_subdrv_register(subdrv);

	return 0;
}

static int fimd_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_context *ctx = platform_get_drvdata(pdev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	rockchip_drm_subdrv_unregister(&ctx->subdrv);

	if (ctx->suspended)
		goto out;

	pm_runtime_set_suspended(dev);
	pm_runtime_put_sync(dev);

out:
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int fimd_suspend(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	/*
	 * do not use pm_runtime_suspend(). if pm_runtime_suspend() is
	 * called here, an error would be returned by that interface
	 * because the usage_count of pm runtime is more than 1.
	 */
	if (!pm_runtime_suspended(dev))
		return fimd_activate(ctx, false);

	return 0;
}

static int fimd_resume(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	/*
	 * if entered to sleep when lcd panel was on, the usage_count
	 * of pm runtime would still be 1 so in this case, fimd driver
	 * should be on directly not drawing on pm runtime interface.
	 */
	if (!pm_runtime_suspended(dev)) {
		int ret;

		ret = fimd_activate(ctx, true);
		if (ret < 0)
			return ret;

		/*
		 * in case of dpms on(standby), fimd_apply function will
		 * be called by encoder's dpms callback to update fimd's
		 * registers but in case of sleep wakeup, it's not.
		 * so fimd_apply function should be called at here.
		 */
		fimd_apply(dev);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int fimd_runtime_suspend(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	return fimd_activate(ctx, false);
}

static int fimd_runtime_resume(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	return fimd_activate(ctx, true);
}
#endif
#if defined(CONFIG_OF)
static const struct of_device_id rk3288_lcdc_dt_ids[] = {
	{.compatible = "rockchip,rk3288-lcdc",},
	{}
};
#endif

static const struct dev_pm_ops fimd_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fimd_suspend, fimd_resume)
	SET_RUNTIME_PM_OPS(fimd_runtime_suspend, fimd_runtime_resume, NULL)
};

struct platform_driver fimd_driver = {
	.probe		= fimd_probe,
	.remove		= fimd_remove,
	.id_table       = rk3288_lcdc_dt_ids,
	.driver		= {
		.name	= "rk3288-lcdc",
		.owner	= THIS_MODULE,
		.pm	= &fimd_pm_ops,
		.of_match_table = of_match_ptr(rk3288_lcdc_dt_ids),
	},
};

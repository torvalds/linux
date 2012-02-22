/* drivers/video/rk30_fb.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_RK30_FB_H
#define __ARCH_ARM_MACH_RK30_FB_H

#define RK30_MAX_LCDC_SUPPORT	4
#define RK30_MAX_LAYER_SUPPORT	4


/********************************************************************
**                            宏定义                                *
********************************************************************/
/* 输往屏的数据格式 */
#define OUT_P888            0
#define OUT_P666            1    //666的屏, 接DATA0-17
#define OUT_P565            2    //565的屏, 接DATA0-15
#define OUT_S888x           4
#define OUT_CCIR656         6
#define OUT_S888            8
#define OUT_S888DUMY        12
#define OUT_P16BPP4         24  //模拟方式,控制器并不支持
#define OUT_D888_P666       0x21  //666的屏, 接DATA2-7, DATA10-15, DATA18-23
#define OUT_D888_P565       0x22  //565的屏, 接DATA3-7, DATA10-15, DATA19-23

enum data_format{
	ARGB888 = 0,
	RGB888,
	RGB565,
	YUV422,
	YUV420,
	YUV444,
};
struct rk_fb_rgb {
	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

typedef enum _TRSP_MODE
{
    TRSP_CLOSE = 0,
    TRSP_FMREG,
    TRSP_FMREGEX,
    TRSP_FMRAM,
    TRSP_FMRAMEX,
    TRSP_MASK,
    TRSP_INVAL
} TRSP_MODE;

struct layer_par {
	u32	pseudo_pal[16];
    u32 y_offset;
    u32 c_offset;
    u32 xpos;         //start point in panel
    u32 ypos;
    u16 xsize;        //size of panel
    u16 ysize;
	u16 xact;        //act size
	u16 yact;
	u16 xres_virtual;
	u16 yres_virtual;
	unsigned long smem_start;
    enum data_format format;
	
	bool support_3d;
	const char *name;
	int id;
};

struct rk_fb_device_driver{
	const char *name;
	int id;
	struct device  *dev;
	
	struct layer_par *layer_par;
	int num_layer;
	rk_screen screen;
	u32 pixclock;
	int (*ioctl)(unsigned int cmd, unsigned long arg,struct layer_par *layer_par);
	int (*suspend)(struct layer_par *layer_par);
	int (*resume)(struct layer_par *layer_par);
	int (*blank)(struct rk_fb_device_driver *rk_fb_dev_drv,int layer_id,int blank_mode);
	int (*set_par)(struct rk_fb_device_driver *rk_fb_dev_drv,int layer_id);
	int (*pan)(struct rk_fb_device_driver *rk_fb_dev_drv,int layer_id);
	
};

struct rk_fb_inf {
    struct fb_info *fb1;
    struct fb_info *fb0;

	struct rk_fb_device_driver *rk_lcdc_device[RK30_MAX_LCDC_SUPPORT];
	
	int num_lcdc;
};
extern int rk_fb_register(struct rk_fb_device_driver *fb_device_driver);
extern int rk_fb_unregister(struct rk_fb_device_driver *fb_device_driver);

#endif

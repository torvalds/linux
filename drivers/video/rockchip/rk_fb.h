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
#define RK_MAX_FB_SUPPORT     4



#define FB0_IOCTL_STOP_TIMER_FLUSH		0x6001
#define FB0_IOCTL_SET_PANEL				0x6002

#ifdef CONFIG_FB_WIMO
#define FB_WIMO_FLAG
#endif
#ifdef FB_WIMO_FLAG
#define FB0_IOCTL_SET_BUF					0x6017
#define FB0_IOCTL_COPY_CURBUF				0x6018
#define FB0_IOCTL_CLOSE_BUF				0x6019
#endif

#define FB1_IOCTL_GET_PANEL_SIZE		0x5001
#define FB1_IOCTL_SET_YUV_ADDR			0x5002
//#define FB1_TOCTL_SET_MCU_DIR			0x5003
#define FB1_IOCTL_SET_ROTATE            0x5003
#define FB1_IOCTL_SET_I2P_ODD_ADDR      0x5005
#define FB1_IOCTL_SET_I2P_EVEN_ADDR     0x5006
#define FB1_IOCTL_SET_WIN0_TOP          0x5018

/********************************************************************
**              display output interface supported by rk lcdc                       *
********************************************************************/
/* */
#define OUT_P888            0
#define OUT_P666            1    //
#define OUT_P565            2    //
#define OUT_S888x           4
#define OUT_CCIR656         6
#define OUT_S888            8
#define OUT_S888DUMY        12
#define OUT_P16BPP4         24  //
#define OUT_D888_P666       0x21  //
#define OUT_D888_P565       0x22  //


//display data format
enum data_format{
	ARGB888 = 0,
	RGB888,
	RGB565,
	YUV420 = 4,
	YUV422,
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
    const char *name;
    int id;
    u32	pseudo_pal[16];
    u32 y_offset;       //yuv/rgb offset
    u32 c_offset;     //cb cr offset
    u32 xpos;         //start point in panel
    u32 ypos;
    u16 xsize;        // display window width
    u16 ysize;          //
    u16 xact;        //origin display window size
    u16 yact;
    u16 xres_virtual;
    u16 yres_virtual;
    unsigned long smem_start;
    unsigned long cbr_start;  // Cbr memory start address
    enum data_format format;
	
    bool support_3d;
    
};

struct rk_lcdc_device_driver{
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
	int (*blank)(struct rk_lcdc_device_driver *rk_fb_dev_drv,int layer_id,int blank_mode);
	int (*set_par)(struct rk_lcdc_device_driver *rk_fb_dev_drv,int layer_id);
	int (*pan)(struct rk_lcdc_device_driver *rk_fb_dev_drv,int layer_id);
	
};

struct rk_fb_inf {
    struct fb_info *fb[RK_MAX_FB_SUPPORT];
    int num_fb;
    
    struct rk_lcdc_device_driver *rk_lcdc_device[RK30_MAX_LCDC_SUPPORT];
    int num_lcdc;

    int video_mode;  //when play video set it to 1
};
extern int rk_fb_register(struct rk_lcdc_device_driver *fb_device_driver);
extern int rk_fb_unregister(struct rk_lcdc_device_driver *fb_device_driver);

#endif

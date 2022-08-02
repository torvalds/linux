/* SPDX-License-Identifier: GPL-2.0 */
/*
 * drivers/video/rockchip/flinger/flinger.c
 *
 * Copyright (C) 2022 Rockchip Electronics Co.Ltd
 *
 */
#ifndef __VEHICLE_FLINGER_H
#define __VEHICLE_FLINGER_H

#include "vehicle_cfg.h"
#include "../rga3/include/rga.h"
#include <linux/types.h>
#include <linux/dma-mapping.h>

int vehicle_flinger_init(struct device *dev, struct vehicle_cfg *v_cfg);
int vehicle_flinger_deinit(void);
int vehicle_flinger_reverse_open(struct vehicle_cfg *cfg,
				bool android_already);
int vehicle_flinger_reverse_close(bool android_already);
unsigned long vehicle_flinger_request_cif_buffer(void);
void vehicle_flinger_commit_cif_buffer(u32 buf_phy_addr);

enum {
	RGA_TRANSFORM_ROT_MASK   =   0x0000000F,
	RGA_TRANSFORM_ROT_0      =   0x00000000,
	RGA_TRANSFORM_ROT_90     =   0x00000001,
	RGA_TRANSFORM_ROT_180    =   0x00000002,
	RGA_TRANSFORM_ROT_270    =   0x00000004,

	RGA_TRANSFORM_FLIP_MASK  =   0x000000F0,
	RGA_TRANSFORM_FLIP_H     =   0x00000020,
	RGA_TRANSFORM_FLIP_V     =   0x00000010,
};
/*
 * pixel format definitions,this is copy from android/system/core/include/system/graphics.h
 */
enum {
	HAL_PIXEL_FORMAT_RGBA_8888 = 1,
	HAL_PIXEL_FORMAT_RGBX_8888 = 2,
	HAL_PIXEL_FORMAT_RGB_888 = 3,
	HAL_PIXEL_FORMAT_RGB_565 = 4,
	HAL_PIXEL_FORMAT_BGRA_8888 = 5,
	HAL_PIXEL_FORMAT_RGBA_5551 = 6,
	HAL_PIXEL_FORMAT_RGBA_4444 = 7,

	/* 0x8 - 0xFF range unavailable */

	/*
	 * 0x100 - 0x1FF
	 *
	 * This range is reserved for pixel formats that are specific to the HAL
	 * implementation.  Implementations can use any value in this range to
	 * communicate video pixel formats between their HAL modules.  These formats
	 * must not have an alpha channel.  Additionally, an EGLimage created from a
	 * gralloc buffer of one of these formats must be supported for use with the
	 * GL_OES_EGL_image_external OpenGL ES extension.
	 */

	/*
	 * Android YUV format:
	 *
	 * This format is exposed outside of the HAL to software decoders and
	 * applications.  EGLImageKHR must support it in conjunction with the
	 * OES_EGL_image_external extension.
	 *
	 * YV12 is a 4:2:0 YCrCb planar format comprised of a WxH Y plane followed
	 * by (W/2) x (H/2) Cr and Cb planes.
	 *
	 * This format assumes
	 * - an even width
	 * - an even height
	 * - a horizontal stride multiple of 16 pixels
	 * - a vertical stride equal to the height
	 *
	 *   y_size = stride * height
	 *   c_size = ALIGN(stride/2, 16) * height/2
	 *   size = y_size + c_size * 2
	 *   cr_offset = y_size
	 *   cb_offset = y_size + c_size
	 *
	 */
	HAL_PIXEL_FORMAT_YV12 = 0x32315659, // YCrCb 4:2:0 Planar

	/* Legacy formats (deprecated), used by ImageFormat.java */

	/*
	 *YCbCr format default is BT601.
	 */
	HAL_PIXEL_FORMAT_YCbCr_422_SP = 0x10,   // NV16
	HAL_PIXEL_FORMAT_YCrCb_420_SP = 0x11,   // NV21
	HAL_PIXEL_FORMAT_YCbCr_422_I = 0x14,    // YUY2
	HAL_PIXEL_FORMAT_YCrCb_NV12 = 0x20, // YUY2
	HAL_PIXEL_FORMAT_YCrCb_NV12_VIDEO = 0x21,   // YUY2

	HAL_PIXEL_FORMAT_YCrCb_NV12_10      = 0x22, // YUV420_1obit
	HAL_PIXEL_FORMAT_YCbCr_422_SP_10    = 0x23, // YUV422_1obit
	HAL_PIXEL_FORMAT_YCrCb_444_SP_10    = 0x24, //YUV444_1obit

	HAL_PIXEL_FORMAT_YCrCb_444 = 0x25,  //yuv444
	HAL_PIXEL_FORMAT_FBDC_RGB565    = 0x26,
	HAL_PIXEL_FORMAT_FBDC_U8U8U8U8  = 0x27, /*ARGB888*/
	HAL_PIXEL_FORMAT_FBDC_U8U8U8    = 0x28, /*RGBP888*/
	HAL_PIXEL_FORMAT_FBDC_RGBA888   = 0x29, /*ABGR888*/
	HAL_PIXEL_FORMAT_BGRX_8888 = 0x30,
	HAL_PIXEL_FORMAT_BGR_888 = 0x31,
	HAL_PIXEL_FORMAT_BGR_565 = 0x32,

	HAL_PIXEL_FORMAT_YUYV422 = 0x33,
	HAL_PIXEL_FORMAT_YUYV420 = 0x34,
	HAL_PIXEL_FORMAT_UYVY422 = 0x35,
	HAL_PIXEL_FORMAT_UYVY420 = 0x36,
};

#endif

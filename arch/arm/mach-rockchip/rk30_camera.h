/*
    camera.h - PXA camera driver header file

    Copyright (C) 2003, Intel Corporation
    Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
#ifndef __ASM_ARCH_CAMERA_RK30_H_
#define __ASM_ARCH_CAMERA_RK30_H_

#define RK29_CAM_DRV_NAME "rk312x-camera"
#define RK_SUPPORT_CIF0   1
#define RK_SUPPORT_CIF1   0
#define RK_CIF_NAME "rk_cif"
#define RK_SENSOR_NAME "rk_sensor"

#include "rk_camera.h"
#include <dt-bindings/pinctrl/rockchip-rk3288.h>

#define CONFIG_CAMERA_INPUT_FMT_SUPPORT     (RK_CAM_INPUT_FMT_YUV422)
#ifdef CONFIG_SOC_RK3028
#define CONFIG_CAMERA_SCALE_CROP_MACHINE    RK_CAM_SCALE_CROP_ARM
#else
#define CONFIG_CAMERA_SCALE_CROP_MACHINE    RK_CAM_SCALE_CROP_IPP
#endif

#if (CONFIG_CAMERA_SCALE_CROP_MACHINE==RK_CAM_SCALE_CROP_ARM)
    #define CAMERA_SCALE_CROP_MACHINE  "arm"
#elif (CONFIG_CAMERA_SCALE_CROP_MACHINE==RK_CAM_SCALE_CROP_IPP)
    #define CAMERA_SCALE_CROP_MACHINE  "ipp"
#elif (CONFIG_CAMERA_SCALE_CROP_MACHINE==RK_CAM_SCALE_CROP_RGA)
    #define CAMERA_SCALE_CROP_MACHINE  "rga"
#elif (CONFIG_CAMERA_SCALE_CROP_MACHINE==RK_CAM_SCALE_CROP_PP)
    #define CAMERA_SCALE_CROP_MACHINE  "pp"
#endif


#define CAMERA_VIDEOBUF_ARM_ACCESS   0

#endif


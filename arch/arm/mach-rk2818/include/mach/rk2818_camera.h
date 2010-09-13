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

#ifndef __ASM_ARCH_CAMERA_H_
#define __ASM_ARCH_CAMERA_H_


#define RK28_CAM_DRV_NAME "rk2818-camera"
#define RK28_CAM_PLATFORM_DEV_ID 33

#define RK28_CAM_SENSOR_NAME_OV9650 "ov9650"
#define RK28_CAM_SENSOR_NAME_OV2655 "ov2655"
#define RK28_CAM_SENSOR_NAME_OV3640 "ov3640"

#define RK28_CAM_POWERACTIVE_BITPOS	0x00
#define RK28_CAM_POWERACTIVE_MASK	(1<<RK28_CAM_POWERACTIVE_BITPOS)
#define RK28_CAM_POWERACTIVE_H	(0x01<<RK28_CAM_POWERACTIVE_BITPOS)
#define RK28_CAM_POWERACTIVE_L	(0x00<<RK28_CAM_POWERACTIVE_BITPOS)

#define RK28_CAM_RESETACTIVE_BITPOS	0x01
#define RK28_CAM_RESETACTIVE_MASK	(1<<RK28_CAM_RESETACTIVE_BITPOS)
#define RK28_CAM_RESETACTIVE_H	(0x01<<RK28_CAM_RESETACTIVE_BITPOS)
#define RK28_CAM_RESETACTIVE_L  (0x00<<RK28_CAM_RESETACTIVE_BITPOS)

struct rk28camera_gpio_res {
    unsigned int gpio_reset;
    unsigned int gpio_power;
    unsigned int gpio_flag;
    const char *dev_name;
};

struct rk28camera_platform_data {
    int (*io_init)(void);
    int (*io_deinit)(void);
    struct rk28camera_gpio_res gpio_res[2];
};

#endif /* __ASM_ARCH_CAMERA_H_ */


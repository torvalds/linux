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


#define RK29_CAM_DRV_NAME "rk29xx-camera"
#define RK29_CAM_PLATFORM_DEV_ID 33

#define INVALID_GPIO -1

#define RK29_CAM_IO_SUCCESS 0
#define RK29_CAM_EIO_INVALID -1
#define RK29_CAM_EIO_REQUESTFAIL -2

#define RK29_CAM_SENSOR_NAME_OV9650 "ov9650"
#define RK29_CAM_SENSOR_NAME_OV2655 "ov2655"
#define RK29_CAM_SENSOR_NAME_OV2659 "ov2659"
#define RK29_CAM_SENSOR_NAME_OV3640 "ov3640"
#define RK29_CAM_SENSOR_NAME_OV5642 "ov5642"

#define RK29_CAM_POWERACTIVE_BITPOS	0x00
#define RK29_CAM_POWERACTIVE_MASK	(1<<RK29_CAM_POWERACTIVE_BITPOS)
#define RK29_CAM_POWERACTIVE_H	(0x01<<RK29_CAM_POWERACTIVE_BITPOS)
#define RK29_CAM_POWERACTIVE_L	(0x00<<RK29_CAM_POWERACTIVE_BITPOS)

#define RK29_CAM_RESETACTIVE_BITPOS	0x01
#define RK29_CAM_RESETACTIVE_MASK	(1<<RK29_CAM_RESETACTIVE_BITPOS)
#define RK29_CAM_RESETACTIVE_H	(0x01<<RK29_CAM_RESETACTIVE_BITPOS)
#define RK29_CAM_RESETACTIVE_L  (0x00<<RK29_CAM_RESETACTIVE_BITPOS)

#define RK29_CAM_POWERDNACTIVE_BITPOS	0x00
#define RK29_CAM_POWERDNACTIVE_MASK	(1<<RK29_CAM_POWERDNACTIVE_BITPOS)
#define RK29_CAM_POWERDNACTIVE_H	(0x01<<RK29_CAM_POWERDNACTIVE_BITPOS)
#define RK29_CAM_POWERDNACTIVE_L	(0x00<<RK29_CAM_POWERDNACTIVE_BITPOS)

#define RK29_CAM_FLASHACTIVE_BITPOS	0x01
#define RK29_CAM_FLASHACTIVE_MASK	(1<<RK29_CAM_FLASHACTIVE_BITPOS)
#define RK29_CAM_FLASHACTIVE_H	(0x01<<RK29_CAM_FLASHACTIVE_BITPOS)
#define RK29_CAM_FLASHACTIVE_L  (0x00<<RK29_CAM_FLASHACTIVE_BITPOS)

/* v4l2_subdev_core_ops.ioctl  ioctl_cmd macro */
#define RK29_CAM_SUBDEV_ACTIVATE            0x00
#define RK29_CAM_SUBDEV_DEACTIVATE          0x01
#define RK29_CAM_SUBDEV_IOREQUEST			0x02

enum rk29camera_ioctrl_cmd
{
	Cam_Power,
	Cam_Reset,
	Cam_PowerDown,
	Cam_Flash
};

enum rk29sensor_power_cmd
{
	Sensor_Reset,
	Sensor_PowerDown,
	Sensor_Flash
};

struct rk29camera_gpio_res {
    unsigned int gpio_reset;
    unsigned int gpio_power;
	unsigned int gpio_powerdown;
	unsigned int gpio_flash;
    unsigned int gpio_flag;
	unsigned int gpio_init;
    const char *dev_name;
};

struct rk29camera_mem_res {
	const char *name;
	unsigned int start;
	unsigned int size;
};
struct rk29camera_platform_data {
    int (*io_init)(void);
    int (*io_deinit)(int sensor);
	int (*sensor_ioctrl)(struct device *dev,enum rk29camera_ioctrl_cmd cmd,int on);
    struct rk29camera_gpio_res gpio_res[2];
	struct rk29camera_mem_res meminfo;
};

#endif /* __ASM_ARCH_CAMERA_H_ */


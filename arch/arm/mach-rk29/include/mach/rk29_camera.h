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

#include <linux/videodev2.h>

#define RK29_CAM_DRV_NAME "rk29xx-camera"
#define RK29_CAM_PLATFORM_DEV_ID 33

#define INVALID_GPIO -1

#define RK29_CAM_IO_SUCCESS 0
#define RK29_CAM_EIO_INVALID -1
#define RK29_CAM_EIO_REQUESTFAIL -2

#define RK29_CAM_SUPPORT_NUMS   6

/*---------------- Camera Sensor Must Define Macro Begin  ------------------------*/
#define RK29_CAM_SENSOR_OV7675 ov7675
#define RK29_CAM_SENSOR_OV9650 ov9650
#define RK29_CAM_SENSOR_OV2640 ov2640
#define RK29_CAM_SENSOR_OV2655 ov2655
#define RK29_CAM_SENSOR_OV2659 ov2659
#define RK29_CAM_SENSOR_OV7690 ov7690
#define RK29_CAM_SENSOR_OV3640 ov3640
#define RK29_CAM_SENSOR_OV5640 ov5640
#define RK29_CAM_SENSOR_OV5642 ov5642
#define RK29_CAM_SENSOR_S5K6AA s5k6aa
#define RK29_CAM_SENSOR_MT9D112 mt9d112
#define RK29_CAM_SENSOR_MT9D113 mt9d113
#define RK29_CAM_SENSOR_MT9P111 mt9p111
#define RK29_CAM_SENSOR_MT9T111 mt9t111
#define RK29_CAM_SENSOR_GT2005  gt2005
#define RK29_CAM_SENSOR_GC0307  gc0307
#define RK29_CAM_SENSOR_GC0308  gc0308
#define RK29_CAM_SENSOR_GC0309  gc0309
#define RK29_CAM_SENSOR_GC2015  gc2015
#define RK29_CAM_SENSOR_SIV120B  siv120b
#define RK29_CAM_SENSOR_SID130B  sid130B
#define RK29_CAM_SENSOR_HI253  hi253
#define RK29_CAM_SENSOR_HI704  hi704
#define RK29_CAM_SENSOR_NT99250 nt99250

#define RK29_CAM_SENSOR_NAME_OV7675 "ov7675"
#define RK29_CAM_SENSOR_NAME_OV9650 "ov9650"
#define RK29_CAM_SENSOR_NAME_OV2640 "ov2640"
#define RK29_CAM_SENSOR_NAME_OV2655 "ov2655"
#define RK29_CAM_SENSOR_NAME_OV2659 "ov2659"
#define RK29_CAM_SENSOR_NAME_OV7690 "ov7690"
#define RK29_CAM_SENSOR_NAME_OV3640 "ov3640"
#define RK29_CAM_SENSOR_NAME_OV5640 "ov5640"
#define RK29_CAM_SENSOR_NAME_OV5642 "ov5642"
#define RK29_CAM_SENSOR_NAME_S5K6AA "s5k6aa"
#define RK29_CAM_SENSOR_NAME_MT9D112 "mt9d112"
#define RK29_CAM_SENSOR_NAME_MT9D113 "mt9d113"
#define RK29_CAM_SENSOR_NAME_MT9P111 "mt9p111"
#define RK29_CAM_SENSOR_NAME_MT9T111 "mt9t111"
#define RK29_CAM_SENSOR_NAME_GT2005  "gt2005"
#define RK29_CAM_SENSOR_NAME_GC0307  "gc0307"
#define RK29_CAM_SENSOR_NAME_GC0308  "gc0308"
#define RK29_CAM_SENSOR_NAME_GC0309  "gc0309"
#define RK29_CAM_SENSOR_NAME_GC2015  "gc2015"
#define RK29_CAM_SENSOR_NAME_SIV120B "siv120b"
#define RK29_CAM_SENSOR_NAME_SID130B "sid130B"
#define RK29_CAM_SENSOR_NAME_HI253  "hi253"
#define RK29_CAM_SENSOR_NAME_HI704  "hi704"
#define RK29_CAM_SENSOR_NAME_NT99250 "nt99250"

#define ov7675_FULL_RESOLUTION     0x30000            // 0.3 megapixel
#define ov9650_FULL_RESOLUTION     0x130000           // 1.3 megapixel   
#define ov2640_FULL_RESOLUTION     0x200000           // 2 megapixel
#define ov2655_FULL_RESOLUTION     0x200000           // 2 megapixel
#define ov2659_FULL_RESOLUTION     0x200000           // 2 megapixel
#define ov7690_FULL_RESOLUTION     0x300000           // 2 megapixel
#define ov3640_FULL_RESOLUTION     0x300000           // 3 megapixel
#define ov5640_FULL_RESOLUTION     0x500000           // 5 megapixel
#define ov5642_FULL_RESOLUTION     0x500000           // 5 megapixel
#define s5k6aa_FULL_RESOLUTION     0x130000           // 1.3 megapixel
#define mt9d112_FULL_RESOLUTION    0x200000           // 2 megapixel
#define mt9d113_FULL_RESOLUTION    0x200000           // 2 megapixel
#define mt9t111_FULL_RESOLUTION    0x300000           // 3 megapixel
#define mt9p111_FULL_RESOLUTION    0x500000           // 5 megapixel
#define gt2005_FULL_RESOLUTION     0x200000           // 2 megapixel
#define gc0308_FULL_RESOLUTION     0x30000            // 0.3 megapixel
#define gc0309_FULL_RESOLUTION     0x30000            // 0.3 megapixel
#define gc2015_FULL_RESOLUTION     0x200000           // 2 megapixel
#define siv120b_FULL_RESOLUTION     0x30000            // 0.3 megapixel
#define sid130B_FULL_RESOLUTION     0x200000           // 2 megapixel    
#define hi253_FULL_RESOLUTION       0x200000           // 2 megapixel
#define hi704_FULL_RESOLUTION       0x30000            // 0.3 megapixel
#define nt99250_FULL_RESOLUTION     0x200000           // 2 megapixel
/*---------------- Camera Sensor Must Define Macro End  ------------------------*/


#define RK29_CAM_POWERACTIVE_BITPOS	0x00
#define RK29_CAM_POWERACTIVE_MASK	(1<<RK29_CAM_POWERACTIVE_BITPOS)
#define RK29_CAM_POWERACTIVE_H	(0x01<<RK29_CAM_POWERACTIVE_BITPOS)
#define RK29_CAM_POWERACTIVE_L	(0x00<<RK29_CAM_POWERACTIVE_BITPOS)

#define RK29_CAM_RESETACTIVE_BITPOS	0x01
#define RK29_CAM_RESETACTIVE_MASK	(1<<RK29_CAM_RESETACTIVE_BITPOS)
#define RK29_CAM_RESETACTIVE_H	(0x01<<RK29_CAM_RESETACTIVE_BITPOS)
#define RK29_CAM_RESETACTIVE_L  (0x00<<RK29_CAM_RESETACTIVE_BITPOS)

#define RK29_CAM_POWERDNACTIVE_BITPOS	0x02
#define RK29_CAM_POWERDNACTIVE_MASK	(1<<RK29_CAM_POWERDNACTIVE_BITPOS)
#define RK29_CAM_POWERDNACTIVE_H	(0x01<<RK29_CAM_POWERDNACTIVE_BITPOS)
#define RK29_CAM_POWERDNACTIVE_L	(0x00<<RK29_CAM_POWERDNACTIVE_BITPOS)

#define RK29_CAM_FLASHACTIVE_BITPOS	0x03
#define RK29_CAM_FLASHACTIVE_MASK	(1<<RK29_CAM_FLASHACTIVE_BITPOS)
#define RK29_CAM_FLASHACTIVE_H	(0x01<<RK29_CAM_FLASHACTIVE_BITPOS)
#define RK29_CAM_FLASHACTIVE_L  (0x00<<RK29_CAM_FLASHACTIVE_BITPOS)

/* v4l2_subdev_core_ops.ioctl  ioctl_cmd macro */
#define RK29_CAM_SUBDEV_ACTIVATE            0x00
#define RK29_CAM_SUBDEV_DEACTIVATE          0x01
#define RK29_CAM_SUBDEV_IOREQUEST			0x02
#define RK29_CAM_SUBDEV_CB_REGISTER         0x03

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

enum rk29camera_flash_cmd
{
    Flash_Off,
    Flash_On,
    Flash_Torch
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
struct rk29camera_info {
    const char *dev_name;
    unsigned int orientation;
    struct v4l2_frmivalenum fival[10];
};
struct rk29camera_platform_data {
    int (*io_init)(void);
    int (*io_deinit)(int sensor);
	int (*sensor_ioctrl)(struct device *dev,enum rk29camera_ioctrl_cmd cmd,int on);
    struct rk29camera_gpio_res gpio_res[RK29_CAM_SUPPORT_NUMS];
	struct rk29camera_mem_res meminfo;
    struct rk29camera_info info[RK29_CAM_SUPPORT_NUMS];
};

struct rk29camera_platform_ioctl_cb {
    int (*sensor_power_cb)(struct rk29camera_gpio_res *res, int on);
    int (*sensor_reset_cb)(struct rk29camera_gpio_res *res, int on);
    int (*sensor_powerdown_cb)(struct rk29camera_gpio_res *res, int on);
    int (*sensor_flash_cb)(struct rk29camera_gpio_res *res, int on);    
};

typedef struct rk29_camera_sensor_cb {
    int (*sensor_cb)(void *arg); 
}rk29_camera_sensor_cb_s;

#endif /* __ASM_ARCH_CAMERA_H_ */


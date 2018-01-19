/*
 * camera.h - PXA camera driver header file
 *
 * Copyright (C) 2003, Intel Corporation
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _RK_CAMERA_H
#define _RK_CAMERA_H

#include <linux/videodev2.h>
#include <media/soc_camera.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/v4l2-mediabus.h>
#include "rk_camera_sensor_info.h"
#include <dt-bindings/pinctrl/rockchip-rk3288.h>

#define RK29_CAM_PLATFORM_DEV_ID 33
#define RK_CAM_PLATFORM_DEV_ID_0 RK29_CAM_PLATFORM_DEV_ID
#define RK_CAM_PLATFORM_DEV_ID_1 (RK_CAM_PLATFORM_DEV_ID_0+1)
#define INVALID_VALUE -1
#ifndef INVALID_GPIO
#define INVALID_GPIO INVALID_VALUE
#endif
#define RK29_CAM_IO_SUCCESS 0
#define RK29_CAM_EIO_INVALID -3
#define RK29_CAM_EIO_REQUESTFAIL -2

#define RK29_CAM_POWERACTIVE_BITPOS	0x00
#define RK29_CAM_RESETACTIVE_BITPOS	0x01
#define RK29_CAM_POWERDNACTIVE_BITPOS 0x02
#define RK29_CAM_FLASHACTIVE_BITPOS	0x03
#define RK29_CAM_AFACTIVE_BITPOS	0x04

#define RK_CAM_SUPPORT_RESOLUTION 0x800000

#define _CONS(a,b) a##b
#define CONS(a,b) _CONS(a,b)

#define _CONS4(a,b,c,d) a##b##c##d
#define CONS4(a,b,c,d) _CONS4(a,b,c,d)

#define __STR(x) #x
#define _STR(x) __STR(x)
#define STR(x) _STR(x)

/*---------------- Camera Sensor Must Define Macro Begin  ------------------------*/
/*
 * move to rk_camera_sensor_info.h   yzm
 */
/*---------------- Camera Sensor Must Define Macro End  ------------------------*/


//#define RK29_CAM_POWERACTIVE_BITPOS	0x00
#define RK29_CAM_POWERACTIVE_MASK	(1<<RK29_CAM_POWERACTIVE_BITPOS)
#define RK29_CAM_POWERACTIVE_H	(0x01<<RK29_CAM_POWERACTIVE_BITPOS)
#define RK29_CAM_POWERACTIVE_L	(0x00<<RK29_CAM_POWERACTIVE_BITPOS)

//#define RK29_CAM_RESETACTIVE_BITPOS	0x01
#define RK29_CAM_RESETACTIVE_MASK	(1<<RK29_CAM_RESETACTIVE_BITPOS)
#define RK29_CAM_RESETACTIVE_H	(0x01<<RK29_CAM_RESETACTIVE_BITPOS)
#define RK29_CAM_RESETACTIVE_L  (0x00<<RK29_CAM_RESETACTIVE_BITPOS)

//#define RK29_CAM_POWERDNACTIVE_BITPOS	0x02
#define RK29_CAM_POWERDNACTIVE_MASK	(1<<RK29_CAM_POWERDNACTIVE_BITPOS)
#define RK29_CAM_POWERDNACTIVE_H	(0x01<<RK29_CAM_POWERDNACTIVE_BITPOS)
#define RK29_CAM_POWERDNACTIVE_L	(0x00<<RK29_CAM_POWERDNACTIVE_BITPOS)

//#define RK29_CAM_FLASHACTIVE_BITPOS	0x03
#define RK29_CAM_FLASHACTIVE_MASK	(1<<RK29_CAM_FLASHACTIVE_BITPOS)
#define RK29_CAM_FLASHACTIVE_H	(0x01<<RK29_CAM_FLASHACTIVE_BITPOS)
#define RK29_CAM_FLASHACTIVE_L  (0x00<<RK29_CAM_FLASHACTIVE_BITPOS)


//#define RK29_CAM_AFACTIVE_BITPOS	0x04
#define RK29_CAM_AFACTIVE_MASK	(1<<RK29_CAM_AFACTIVE_BITPOS)
#define RK29_CAM_AFACTIVE_H	(0x01<<RK29_CAM_AFACTIVE_BITPOS)
#define RK29_CAM_AFACTIVE_L  (0x00<<RK29_CAM_AFACTIVE_BITPOS)



#define RK_CAM_SCALE_CROP_ARM      0
#define RK_CAM_SCALE_CROP_IPP      1
#define RK_CAM_SCALE_CROP_RGA      2
#define RK_CAM_SCALE_CROP_PP       3

#define RK_CAM_INPUT_FMT_YUV422    (1<<0)
#define RK_CAM_INPUT_FMT_RAW10     (1<<1)
#define RK_CAM_INPUT_FMT_RAW12     (1<<2)

#define RK_CAM_INPUT_VIDEO_STATE_LOSS	0x01
#define RK_CAM_INPUT_VIDEO_STATE_LOCKED	0x00		//default video state value is 0x00

/* v4l2_subdev_core_ops.ioctl  ioctl_cmd macro */
#define RK29_CAM_SUBDEV_ACTIVATE            0x00
#define RK29_CAM_SUBDEV_DEACTIVATE          0x01
#define RK29_CAM_SUBDEV_IOREQUEST			0x02
#define RK29_CAM_SUBDEV_CB_REGISTER         0x03
#define RK29_CAM_SUBDEV_GET_INTERFACE		0x04
#define RK29_CAM_SUBDEV_GET_VIDEO_STATE		0x05

#define Sensor_HasBeen_PwrOff(a)            (a&0x01)
#define Sensor_Support_DirectResume(a)      ((a&0x10)==0x10)

#define Sensor_CropSet(a,b)                  a->reserved[1] = b;
#define Sensor_CropGet(a)                    a->reserved[1]

#define RK29_CAM_SUBDEV_HDR_EXPOSURE        0x04

#define RK_VIDEOBUF_HDR_EXPOSURE_MINUS_1        0x00
#define RK_VIDEOBUF_HDR_EXPOSURE_NORMAL         0x01
#define RK_VIDEOBUF_HDR_EXPOSURE_PLUS_1         0x02
#define RK_VIDEOBUF_HDR_EXPOSURE_FINISH         0x03
#define RK_VIDEOBUF_CODE_SET(rk_code,type)  rk_code = (('R'<<24)|('K'<<16)|type)
#define RK_VIDEOBUF_CODE_CHK(rk_code)       ((rk_code&(('R'<<24)|('K'<<16)))==(('R'<<24)|('K'<<16)))

#define CONFIG_CAMERA_INPUT_FMT_SUPPORT     (RK_CAM_INPUT_FMT_YUV422)
#ifdef CONFIG_SOC_RK3028
#define CONFIG_CAMERA_SCALE_CROP_MACHINE    RK_CAM_SCALE_CROP_ARM
#else
#define CONFIG_CAMERA_SCALE_CROP_MACHINE    RK_CAM_SCALE_CROP_IPP
#endif
#if (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_ARM)
    #define CAMERA_SCALE_CROP_MACHINE  "arm"
#elif (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_IPP)
    #define CAMERA_SCALE_CROP_MACHINE  "ipp"
#elif (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_RGA)
    #define CAMERA_SCALE_CROP_MACHINE  "rga"
#elif (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_PP)
    #define CAMERA_SCALE_CROP_MACHINE  "pp"
#endif

#define RK29_CAM_DRV_NAME "rk312x-camera"
#define CAMERA_VIDEOBUF_ARM_ACCESS   0

enum rk29camera_ioctrl_cmd
{
	Cam_Power,
	Cam_Reset,
	Cam_PowerDown,
	Cam_Flash,
	Cam_Mclk,
	Cam_Af
};

enum rk29sensor_power_cmd
{
    Sensor_Power,
	Sensor_Reset,
	Sensor_PowerDown,
	Sensor_Flash,
	Sensor_Af
};

enum rk29camera_flash_cmd
{
    Flash_Off,
    Flash_On,
    Flash_Torch
};

struct rk29_camera_gpio {
	struct gpio_desc *gpio_desc;
	unsigned int pltfrm_gpio;
	int count;

	struct list_head gpios;
};

struct rk29camera_gpio_res {
	struct gpio_desc *gpio_reset;
	struct gpio_desc *gpio_power;
	struct gpio_desc *gpio_powerdown;
	struct gpio_desc *gpio_flash;
	struct gpio_desc *gpio_af;
	struct gpio_desc *gpio_irq;
	int reset;
	int power;
	int powerdown;
	int flash;
	int af;
	int irq;
	unsigned int gpio_flag;
	unsigned int gpio_init;
	const char *dev_name;
};

struct rk29camera_mem_res {
	const char *name;
	unsigned int start;
	unsigned int size;
    void __iomem *vbase;  //指向IO空间的指针，为了驱动程序的通用性考虑
};
struct rk29camera_info {
    const char *dev_name;
    unsigned int orientation;
    struct v4l2_frmivalenum fival[10];
};

struct reginfo_t
{
	u16 reg;
	u16 val;
	u16 reg_len;
	u16 rev;
};
typedef struct rk_sensor_user_init_data{
	int rk_sensor_init_width;
	int rk_sensor_init_height;
	unsigned long rk_sensor_init_bus_param;
	//enum v4l2_mbus_pixelcode rk_sensor_init_pixelcode;
	struct reginfo_t * rk_sensor_init_data;
	int rk_sensor_winseq_size;
	struct reginfo_t * rk_sensor_init_winseq;
	int rk_sensor_init_data_size;
}rk_sensor_user_init_data_s;

typedef struct rk_camera_device_register_info {
    struct i2c_board_info i2c_cam_info;
	struct soc_camera_desc desc_info;/*yzm*/
    struct platform_device device_info;
}rk_camera_device_register_info_t;

enum rk_camera_signal_polarity {
	RK_CAMERA_DEVICE_SIGNAL_HIGH_LEVEL = 1,
	RK_CAMERA_DEVICE_SIGNAL_LOW_LEVEL = 0,
};

enum rk_camera_device_type {
	RK_CAMERA_DEVICE_BT601_PIONGPONG	= 0x10000010,
	RK_CAMERA_DEVICE_BT601_8	= 0x10000011,
	RK_CAMERA_DEVICE_BT601_10	= 0x10000012,
	RK_CAMERA_DEVICE_BT601_12	= 0x10000014,
	RK_CAMERA_DEVICE_BT601_16	= 0x10000018,

	RK_CAMERA_DEVICE_BT656_8	= 0x10000021,
	RK_CAMERA_DEVICE_BT656_10	= 0x10000022,
	RK_CAMERA_DEVICE_BT656_12	= 0x10000024,
	RK_CAMERA_DEVICE_BT656_16	= 0x10000028,

	RK_CAMERA_DEVICE_CVBS_NTSC	= 0x20000001,
	RK_CAMERA_DEVICE_CVBS_PAL	= 0x20000002,
	RK_CAMERA_DEVICE_CVBS_DEINTERLACE	= 0x20000003,
};

struct rk_camera_dvp_config {
	enum rk_camera_signal_polarity vsync;
	enum rk_camera_signal_polarity hsync;
};

struct rk_camera_device_signal_config {
	enum rk_camera_device_type type;
	u32 code;
	struct rk_camera_dvp_config dvp;
	struct v4l2_rect crop;
};

struct rk_camera_device_defrect {
	unsigned int width;
	unsigned int height;
	struct v4l2_rect defrect;
	const char *interface;
};

struct rk_camera_device_channel_info {
	unsigned int channel_total;
	unsigned int default_id;
	const char *channel_info[5];
};

struct rkcamera_platform_data {
    rk_camera_device_register_info_t dev;
    char dev_name[32];
    struct rk29camera_gpio_res io;
    int orientation;
    int resolution;
    int mirror;       /* bit0:  0: mirror off
                                1: mirror on
                         bit1:  0: flip off
                                1: flip on
                      */
    bool flash;       /* true:  the sensor attached flash;
                         false: the sensor haven't attach flash;

                      */
    int pwdn_info;    /* bit4: 1: sensor isn't need to be init after exit stanby, it can streaming directly
                               0: sensor must be init after exit standby;

                         bit0: 1: sensor power have been turn off;
                               0: sensor power is always on;
                      */

    long powerup_sequence;       /*
                                    bit0-bit3 --- power up sequence first step;
                                    bit4-bit7 --- power up sequence second step;
                                     .....
                                  */
    int mclk_rate;       /* MHz : 24/48 */
    int fov_h;           /* fied of view horizontal */
    int fov_v;           /* fied of view vertical */
	int if_powerdown;	/* Whether to control powerdown pin when enter/exit camera;
					true: control; false: don't control;*/
	const char *power_pmu_name1;
	const char *power_pmu_name2;
	const char *powerdown_pmu_name;
	int power_pmu_voltage1;
	int power_pmu_voltage2;
	int powerdown_pmu_voltage;
	struct device_node *of_node;
	struct rkcamera_platform_data *next_camera;/*yzm*/
	struct rk_camera_device_defrect defrects[4];
	struct rk_camera_device_channel_info channel_info;
};

struct rk29camera_platform_data {
    int (*io_init)(void);
    int (*io_deinit)(int sensor);
	int (*sensor_ioctrl)(struct device *dev,enum rk29camera_ioctrl_cmd cmd,int on);

    int (*sensor_register)(void);
    int (*sensor_mclk)(int cif_idx, int on, int clk_rate);

    struct rkcamera_platform_data *register_dev_new;  //sensor
	struct device *cif_dev;/*yzm host*/
	const char *rockchip_name;
	int iommu_enabled;
};

struct rk29camera_platform_ioctl_cb {
    int (*sensor_power_cb)(struct rk29camera_gpio_res *res, int on);
    int (*sensor_reset_cb)(struct rk29camera_gpio_res *res, int on);
    int (*sensor_powerdown_cb)(struct rk29camera_gpio_res *res, int on);
    int (*sensor_flash_cb)(struct rk29camera_gpio_res *res, int on);
    int (*sensor_af_cb)(struct rk29camera_gpio_res *res, int on);
};

typedef struct rk29_camera_sensor_cb {
    int (*sensor_cb)(void *arg);
    int (*scale_crop_cb)(struct work_struct *work);
}rk29_camera_sensor_cb_s;

#endif /* _RK_CAMERA_H */


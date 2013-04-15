/*
 * Driver for HM5065 CMOS Image Sensor from Himax
 *
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/circ_buf.h>
#include <linux/miscdevice.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <plat/rk_camera.h>
#include "hm5065.h"
//#include <linux/wine_sysinf.h>
struct reginfo
{
    u16 reg;
    u8 val;
};
static int debug=1;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

#define SENSOR_TR(format, ...) printk(KERN_ERR format, ## __VA_ARGS__)
#define SENSOR_DG(format, ...) dprintk(1, format, ## __VA_ARGS__)

#define _CONS(a,b) a##b
#define CONS(a,b) _CONS(a,b)

#define __STR(x) #x
#define _STR(x) __STR(x)
#define STR(x) _STR(x)

#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)

/* Sensor Driver Configuration */
#define SENSOR_NAME RK29_CAM_SENSOR_HM5065
#define SENSOR_V4L2_IDENT V4L2_IDENT_HM5065
#define SENSOR_ID 0x039E
#define SENSOR_MIN_WIDTH    176
#define SENSOR_MIN_HEIGHT   144
#define SENSOR_MAX_WIDTH    2592
#define SENSOR_MAX_HEIGHT   1944
#define SENSOR_INIT_WIDTH	  800			//640/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  600   //480
#define SENSOR_INIT_WINSEQADR sensor_svga
#define SENSOR_INIT_PIXFMT V4L2_MBUS_FMT_YUYV8_2X8

#define CONFIG_SENSOR_WhiteBalance	1
#define CONFIG_SENSOR_Brightness	1
#define CONFIG_SENSOR_Contrast      1
#define CONFIG_SENSOR_Saturation    1
#define CONFIG_SENSOR_Effect        1
#define CONFIG_SENSOR_Scene         1
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Exposure      0
#define CONFIG_SENSOR_Flash         1
#define CONFIG_SENSOR_Mirror        0
#define CONFIG_SENSOR_Flip          0
#define CONFIG_SENSOR_Focus         1

#define CONFIG_SENSOR_I2C_SPEED    100000       /* Hz */
/* Sensor write register continues by preempt_disable/preempt_enable for current process not be scheduled */
#define CONFIG_SENSOR_I2C_NOSCHED   0
#define CONFIG_SENSOR_I2C_RDWRCHK   0

#define CONFIG_SENSOR_WRITE_REGS  1
#define WRITE_REGS_NUM 3

#define SENSOR_BUS_PARAM  (SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING |\
                          SOCAM_HSYNC_ACTIVE_HIGH| SOCAM_VSYNC_ACTIVE_LOW |\
                          SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8  |SOCAM_MCLK_24MHZ)

#define COLOR_TEMPERATURE_CLOUDY_DN  6500
#define COLOR_TEMPERATURE_CLOUDY_UP    8000
#define COLOR_TEMPERATURE_CLEARDAY_DN  5000
#define COLOR_TEMPERATURE_CLEARDAY_UP    6500
#define COLOR_TEMPERATURE_OFFICE_DN     3500
#define COLOR_TEMPERATURE_OFFICE_UP     5000
#define COLOR_TEMPERATURE_HOME_DN       2500
#define COLOR_TEMPERATURE_HOME_UP       3500

#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)

#define SENSOR_AF_IS_ERR    (0x00<<0)
#define SENSOR_AF_IS_OK		(0x01<<0)
#define SENSOR_INIT_IS_ERR   (0x00<<28)
#define SENSOR_INIT_IS_OK    (0x01<<28)

#if CONFIG_SENSOR_Focus
/*#define SENSOR_AF_MODE_INFINITY    0
#define SENSOR_AF_MODE_MACRO       1
#define SENSOR_AF_MODE_FIXED       2
#define SENSOR_AF_MODE_AUTO        3
#define SENSOR_AF_MODE_CONTINUOUS  4
#define SENSOR_AF_MODE_CLOSE       5*/
#define SENSOR_AF_MODE_AUTO        0
#define SENSOR_AF_MODE_CLOSE       1
#define SENSOR_AF_MODE_CONTINUOUS  2
#endif

#if CONFIG_SENSOR_Focus
/* hm5065 VCM Command and Status Registers */
#define AF_TARGETPOS_REG_H         0x0700
#define AF_TARGETPOS_REG_L         0x0701

#define AF_MODES_REG               0x070A
#define AF_AUTOCMDS_REG            0x070b
#define AF_LENSPOS_REG_H           0x06F0
#define AF_LENSPOS_REG_L           0x06F1
#define AF_MANUCMDS_REG            0x070c
#define AF_FINISHCHK_REG           0x07ae   //[0]: false    [1] : true

#define FACE_LC                    0x0714   //[0] : focus , [1] : AE ��
#define FACE_START_XH 	           0x0715
#define FACE_START_XL 	           0x0716
#define FACE_SIZE_XH  	           0x0717
#define FACE_SIZE_XL	           0x0718
#define FACE_START_YH	           0x0719
#define FACE_START_YL	           0x071A
#define FACE_SIZE_YH	           0x071B
#define FACE_SIZE_YL 	           0x071C


#define AF_ZONE0_WEIGHT            0x0808
#define AF_ZONE1_WEIGHT            0x0809
#define AF_ZONE2_WEIGHT            0x080A
#define AF_ZONE3_WEIGHT            0x080B
#define AF_ZONE4_WEIGHT            0x080C
#define AF_ZONE5_WEIGHT            0x080D
#define AF_ZONE6_WEIGHT            0x080E

#endif

//flash off in fixed time to prevent from too hot , zyc
struct  flash_timer{
    struct soc_camera_device *icd;
	struct hrtimer timer;
};
static enum hrtimer_restart flash_off_func(struct hrtimer *timer);

static struct  flash_timer flash_off_timer;
//for user defined if user want to customize the series , zyc
#ifdef CONFIG_HM5065_USER_DEFINED_SERIES
#include "hm5065_user_series.c"
#else


static struct reginfo sensor_init_data[] =
{
#if 1
	{0xffff,0x01},    // 2013-03-01 Start new patch 
	{0x9000,0x03}, 
	{0xA000,0x90}, 
	{0xA001,0x0C}, 
	{0xA002,0x56}, 
	{0xA003,0xE0}, 
	{0xA004,0xFE}, 
	{0xA005,0xA3}, 
	{0xA006,0xE0}, 
	{0xA007,0xFF}, 
	{0xA008,0x12}, 
	{0xA009,0x42}, 
	{0xA00A,0x85}, 
	{0xA00B,0x90}, 
	{0xA00C,0x01}, 
	{0xA00D,0xB7}, 
	{0xA00E,0xEE}, 
	{0xA00F,0xF0}, 
	{0xA010,0xFC}, 
	{0xA011,0xA3}, 
	{0xA012,0xEF}, 
	{0xA013,0xF0}, 
	{0xA014,0xFD}, 
	{0xA015,0x90}, 
	{0xA016,0x06}, 
	{0xA017,0x05}, 
	{0xA018,0xE0}, 
	{0xA019,0x75}, 
	{0xA01A,0xF0}, 
	{0xA01B,0x02}, 
	{0xA01C,0xA4}, 
	{0xA01D,0x2D}, 
	{0xA01E,0xFF}, 
	{0xA01F,0xE5}, 
	{0xA020,0xF0}, 
	{0xA021,0x3C}, 
	{0xA022,0xFE}, 
	{0xA023,0xAB}, 
	{0xA024,0x07}, 
	{0xA025,0xFA}, 
	{0xA026,0x33}, 
	{0xA027,0x95}, 
	{0xA028,0xE0}, 
	{0xA029,0xF9}, 
	{0xA02A,0xF8}, 
	{0xA02B,0x90}, 
	{0xA02C,0x0B}, 
	{0xA02D,0x4B}, 
	{0xA02E,0xE0}, 
	{0xA02F,0xFE}, 
	{0xA030,0xA3}, 
	{0xA031,0xE0}, 
	{0xA032,0xFF}, 
	{0xA033,0xEE}, 
	{0xA034,0x33}, 
	{0xA035,0x95}, 
	{0xA036,0xE0}, 
	{0xA037,0xFD}, 
	{0xA038,0xFC}, 
	{0xA039,0x12}, 
	{0xA03A,0x0C}, 
	{0xA03B,0x7B}, 
	{0xA03C,0x90}, 
	{0xA03D,0x01}, 
	{0xA03E,0xB9}, 
	{0xA03F,0x12}, 
	{0xA040,0x0E}, 
	{0xA041,0x05}, 
	{0xA042,0x90}, 
	{0xA043,0x01}, 
	{0xA044,0xB9}, 
	{0xA045,0xE0}, 
	{0xA046,0xFC}, 
	{0xA047,0xA3}, 
	{0xA048,0xE0}, 
	{0xA049,0xFD}, 
	{0xA04A,0xA3}, 
	{0xA04B,0xE0}, 
	{0xA04C,0xFE}, 
	{0xA04D,0xA3}, 
	{0xA04E,0xE0}, 
	{0xA04F,0xFF}, 
	{0xA050,0x78}, 
	{0xA051,0x08}, 
	{0xA052,0x12}, 
	{0xA053,0x0D}, 
	{0xA054,0xBF}, 
	{0xA055,0xA8}, 
	{0xA056,0x04}, 
	{0xA057,0xA9}, 
	{0xA058,0x05}, 
	{0xA059,0xAA}, 
	{0xA05A,0x06}, 
	{0xA05B,0xAB}, 
	{0xA05C,0x07}, 
	{0xA05D,0x90}, 
	{0xA05E,0x0B}, 
	{0xA05F,0x49}, 
	{0xA060,0xE0}, 
	{0xA061,0xFE}, 
	{0xA062,0xA3}, 
	{0xA063,0xE0}, 
	{0xA064,0xFF}, 
	{0xA065,0xEE}, 
	{0xA066,0x33}, 
	{0xA067,0x95}, 
	{0xA068,0xE0}, 
	{0xA069,0xFD}, 
	{0xA06A,0xFC}, 
	{0xA06B,0xC3}, 
	{0xA06C,0xEF}, 
	{0xA06D,0x9B}, 
	{0xA06E,0xFF}, 
	{0xA06F,0xEE}, 
	{0xA070,0x9A}, 
	{0xA071,0xFE}, 
	{0xA072,0xED}, 
	{0xA073,0x99}, 
	{0xA074,0xFD}, 
	{0xA075,0xEC}, 
	{0xA076,0x98}, 
	{0xA077,0xFC}, 
	{0xA078,0x78}, 
	{0xA079,0x01}, 
	{0xA07A,0x12}, 
	{0xA07B,0x0D}, 
	{0xA07C,0xBF}, 
	{0xA07D,0x90}, 
	{0xA07E,0x0C}, 
	{0xA07F,0x4A}, 
	{0xA080,0xE0}, 
	{0xA081,0xFC}, 
	{0xA082,0xA3}, 
	{0xA083,0xE0}, 
	{0xA084,0xF5}, 
	{0xA085,0x82}, 
	{0xA086,0x8C}, 
	{0xA087,0x83}, 
	{0xA088,0xC0}, 
	{0xA089,0x83}, 
	{0xA08A,0xC0}, 
	{0xA08B,0x82}, 
	{0xA08C,0x90}, 
	{0xA08D,0x0B}, 
	{0xA08E,0x48}, 
	{0xA08F,0xE0}, 
	{0xA090,0xD0}, 
	{0xA091,0x82}, 
	{0xA092,0xD0}, 
	{0xA093,0x83}, 
	{0xA094,0x75}, 
	{0xA095,0xF0}, 
	{0xA096,0x02}, 
	{0xA097,0x12}, 
	{0xA098,0x0E}, 
	{0xA099,0x45}, 
	{0xA09A,0xEE}, 
	{0xA09B,0xF0}, 
	{0xA09C,0xA3}, 
	{0xA09D,0xEF}, 
	{0xA09E,0xF0}, 
	{0xA09F,0x02}, 
	{0xA0A0,0xBA}, 
	{0xA0A1,0xD8}, 
	{0xA0A2,0x90}, 
	{0xA0A3,0x30}, 
	{0xA0A4,0x18}, 
	{0xA0A5,0xe4}, 
	{0xA0A6,0xf0}, 
	{0xA0A7,0x74}, 
	{0xA0A8,0x3f}, 
	{0xA0A9,0xf0}, 
	{0xA0AA,0x22}, 
	{0xA0BF,0x90}, 
	{0xA0C0,0x00}, 
	{0xA0C1,0x5E}, 
	{0xA0C2,0xE0}, 
	{0xA0C3,0xFF}, 
	{0xA0C4,0x70}, 
	{0xA0C5,0x20}, 
	{0xA0C6,0x90}, 
	{0xA0C7,0x47}, 
	{0xA0C8,0x04}, 
	{0xA0C9,0x74}, 
	{0xA0CA,0x0A}, 
	{0xA0CB,0xF0}, 
	{0xA0CC,0xA3}, 
	{0xA0CD,0x74}, 
	{0xA0CE,0x30}, 
	{0xA0CF,0xF0}, 
	{0xA0D0,0x90}, 
	{0xA0D1,0x47}, 
	{0xA0D2,0x0C}, 
	{0xA0D3,0x74}, 
	{0xA0D4,0x07}, 
	{0xA0D5,0xF0}, 
	{0xA0D6,0xA3}, 
	{0xA0D7,0x74}, 
	{0xA0D8,0xA8}, 
	{0xA0D9,0xF0}, 
	{0xA0DA,0x90}, 
	{0xA0DB,0x47}, 
	{0xA0DC,0xA4}, 
	{0xA0DD,0x74}, 
	{0xA0DE,0x01}, 
	{0xA0DF,0xF0}, 
	{0xA0E0,0x90}, 
	{0xA0E1,0x47}, 
	{0xA0E2,0xA8}, 
	{0xA0E3,0xF0}, 
	{0xA0E4,0x80}, 
	{0xA0E5,0x50}, 
	{0xA0E6,0xEF}, 
	{0xA0E7,0x64}, 
	{0xA0E8,0x01}, 
	{0xA0E9,0x60}, 
	{0xA0EA,0x04}, 
	{0xA0EB,0xEF}, 
	{0xA0EC,0xB4}, 
	{0xA0ED,0x03}, 
	{0xA0EE,0x20}, 
	{0xA0EF,0x90}, 
	{0xA0F0,0x47}, 
	{0xA0F1,0x04}, 
	{0xA0F2,0x74}, 
	{0xA0F3,0x05}, 
	{0xA0F4,0xF0}, 
	{0xA0F5,0xA3}, 
	{0xA0F6,0x74}, 
	{0xA0F7,0x18}, 
	{0xA0F8,0xF0}, 
	{0xA0F9,0x90}, 
	{0xA0FA,0x47}, 
	{0xA0FB,0x0C}, 
	{0xA0FC,0x74}, 
	{0xA0FD,0x03}, 
	{0xA0FE,0xF0}, 
	{0xA0FF,0xA3}, 
	{0xA100,0x74}, 
	{0xA101,0xD4}, 
	{0xA102,0xF0}, 
	{0xA103,0x90}, 
	{0xA104,0x47}, 
	{0xA105,0xA4}, 
	{0xA106,0x74}, 
	{0xA107,0x02}, 
	{0xA108,0xF0}, 
	{0xA109,0x90}, 
	{0xA10A,0x47}, 
	{0xA10B,0xA8}, 
	{0xA10C,0xF0}, 
	{0xA10D,0x80}, 
	{0xA10E,0x27}, 
	{0xA10F,0xEF}, 
	{0xA110,0x64}, 
	{0xA111,0x02}, 
	{0xA112,0x60}, 
	{0xA113,0x04}, 
	{0xA114,0xEF}, 
	{0xA115,0xB4}, 
	{0xA116,0x04}, 
	{0xA117,0x1E}, 
	{0xA118,0x90}, 
	{0xA119,0x47}, 
	{0xA11A,0x04}, 
	{0xA11B,0x74}, 
	{0xA11C,0x02}, 
	{0xA11D,0xF0}, 
	{0xA11E,0xA3}, 
	{0xA11F,0x74}, 
	{0xA120,0x8C}, 
	{0xA121,0xF0}, 
	{0xA122,0x90}, 
	{0xA123,0x47}, 
	{0xA124,0x0C}, 
	{0xA125,0x74}, 
	{0xA126,0x01}, 
	{0xA127,0xF0}, 
	{0xA128,0xA3}, 
	{0xA129,0x74}, 
	{0xA12A,0xEA}, 
	{0xA12B,0xF0}, 
	{0xA12C,0x90}, 
	{0xA12D,0x47}, 
	{0xA12E,0xA4}, 
	{0xA12F,0x74}, 
	{0xA130,0x04}, 
	{0xA131,0xF0}, 
	{0xA132,0x90}, 
	{0xA133,0x47}, 
	{0xA134,0xA8}, 
	{0xA135,0xF0}, 
	{0xA136,0x22}, 
	{0xA137,0x74}, 
	{0xA138,0x04}, 
	{0xA139,0xF0}, 
	{0xA13A,0xA3}, 
	{0xA13B,0x74}, 
	{0xA13C,0x20}, 
	{0xA13D,0xF0}, 
	{0xA13E,0xE4}, 
	{0xA13F,0xF5}, 
	{0xA140,0x22}, 
	{0xA141,0xE5}, 
	{0xA142,0x22}, 
	{0xA143,0xC3}, 
	{0xA144,0x94}, 
	{0xA145,0x40}, 
	{0xA146,0x40}, 
	{0xA147,0x03}, 
	{0xA148,0x02}, 
	{0xA149,0xF1}, 
	{0xA14A,0xFD}, 
	{0xA14B,0x90}, 
	{0xA14C,0x0A}, 
	{0xA14D,0xBA}, 
	{0xA14E,0xE0}, 
	{0xA14F,0xFE}, 
	{0xA150,0xA3}, 
	{0xA151,0xE0}, 
	{0xA152,0xFF}, 
	{0xA153,0xF5}, 
	{0xA154,0x82}, 
	{0xA155,0x8E}, 
	{0xA156,0x83}, 
	{0xA157,0xE0}, 
	{0xA158,0x54}, 
	{0xA159,0x70}, 
	{0xA15A,0xFD}, 
	{0xA15B,0xC4}, 
	{0xA15C,0x54}, 
	{0xA15D,0x0F}, 
	{0xA15E,0xFD}, 
	{0xA15F,0x90}, 
	{0xA160,0x0A}, 
	{0xA161,0xBC}, 
	{0xA162,0xE0}, 
	{0xA163,0xFA}, 
	{0xA164,0xA3}, 
	{0xA165,0xE0}, 
	{0xA166,0xF5}, 
	{0xA167,0x82}, 
	{0xA168,0x8A}, 
	{0xA169,0x83}, 
	{0xA16A,0xED}, 
	{0xA16B,0xF0}, 
	{0xA16C,0x90}, 
	{0xA16D,0x0A}, 
	{0xA16E,0xBD}, 
	{0xA16F,0xE0}, 
	{0xA170,0x04}, 
	{0xA171,0xF0}, 
	{0xA172,0x70}, 
	{0xA173,0x06}, 
	{0xA174,0x90}, 
	{0xA175,0x0A}, 
	{0xA176,0xBC}, 
	{0xA177,0xE0}, 
	{0xA178,0x04}, 
	{0xA179,0xF0}, 
	{0xA17A,0x8F}, 
	{0xA17B,0x82}, 
	{0xA17C,0x8E}, 
	{0xA17D,0x83}, 
	{0xA17E,0xA3}, 
	{0xA17F,0xE0}, 
	{0xA180,0xFF}, 
	{0xA181,0x90}, 
	{0xA182,0x0A}, 
	{0xA183,0xBC}, 
	{0xA184,0xE0}, 
	{0xA185,0xFC}, 
	{0xA186,0xA3}, 
	{0xA187,0xE0}, 
	{0xA188,0xF5}, 
	{0xA189,0x82}, 
	{0xA18A,0x8C}, 
	{0xA18B,0x83}, 
	{0xA18C,0xEF}, 
	{0xA18D,0xF0}, 
	{0xA18E,0x90}, 
	{0xA18F,0x0A}, 
	{0xA190,0xBD}, 
	{0xA191,0xE0}, 
	{0xA192,0x04}, 
	{0xA193,0xF0}, 
	{0xA194,0x70}, 
	{0xA195,0x06}, 
	{0xA196,0x90}, 
	{0xA197,0x0A}, 
	{0xA198,0xBC}, 
	{0xA199,0xE0}, 
	{0xA19A,0x04}, 
	{0xA19B,0xF0}, 
	{0xA19C,0x90}, 
	{0xA19D,0x0A}, 
	{0xA19E,0xBA}, 
	{0xA19F,0xE0}, 
	{0xA1A0,0xFE}, 
	{0xA1A1,0xA3}, 
	{0xA1A2,0xE0}, 
	{0xA1A3,0xFF}, 
	{0xA1A4,0xF5}, 
	{0xA1A5,0x82}, 
	{0xA1A6,0x8E}, 
	{0xA1A7,0x83}, 
	{0xA1A8,0xE0}, 
	{0xA1A9,0x54}, 
	{0xA1AA,0x07}, 
	{0xA1AB,0xFD}, 
	{0xA1AC,0x90}, 
	{0xA1AD,0x0A}, 
	{0xA1AE,0xBC}, 
	{0xA1AF,0xE0}, 
	{0xA1B0,0xFA}, 
	{0xA1B1,0xA3}, 
	{0xA1B2,0xE0}, 
	{0xA1B3,0xF5}, 
	{0xA1B4,0x82}, 
	{0xA1B5,0x8A}, 
	{0xA1B6,0x83}, 
	{0xA1B7,0xED}, 
	{0xA1B8,0xF0}, 
	{0xA1B9,0x90}, 
	{0xA1BA,0x0A}, 
	{0xA1BB,0xBD}, 
	{0xA1BC,0xE0}, 
	{0xA1BD,0x04}, 
	{0xA1BE,0xF0}, 
	{0xA1BF,0x70}, 
	{0xA1C0,0x06}, 
	{0xA1C1,0x90}, 
	{0xA1C2,0x0A}, 
	{0xA1C3,0xBC}, 
	{0xA1C4,0xE0}, 
	{0xA1C5,0x04}, 
	{0xA1C6,0xF0}, 
	{0xA1C7,0x8F}, 
	{0xA1C8,0x82}, 
	{0xA1C9,0x8E}, 
	{0xA1CA,0x83}, 
	{0xA1CB,0xA3}, 
	{0xA1CC,0xA3}, 
	{0xA1CD,0xE0}, 
	{0xA1CE,0xFF}, 
	{0xA1CF,0x90}, 
	{0xA1D0,0x0A}, 
	{0xA1D1,0xBC}, 
	{0xA1D2,0xE0}, 
	{0xA1D3,0xFC}, 
	{0xA1D4,0xA3}, 
	{0xA1D5,0xE0}, 
	{0xA1D6,0xF5}, 
	{0xA1D7,0x82}, 
	{0xA1D8,0x8C}, 
	{0xA1D9,0x83}, 
	{0xA1DA,0xEF}, 
	{0xA1DB,0xF0}, 
	{0xA1DC,0x90}, 
	{0xA1DD,0x0A}, 
	{0xA1DE,0xBD}, 
	{0xA1DF,0xE0}, 
	{0xA1E0,0x04}, 
	{0xA1E1,0xF0}, 
	{0xA1E2,0x70}, 
	{0xA1E3,0x06}, 
	{0xA1E4,0x90}, 
	{0xA1E5,0x0A}, 
	{0xA1E6,0xBC}, 
	{0xA1E7,0xE0}, 
	{0xA1E8,0x04}, 
	{0xA1E9,0xF0}, 
	{0xA1EA,0x90}, 
	{0xA1EB,0x0A}, 
	{0xA1EC,0xBB}, 
	{0xA1ED,0xE0}, 
	{0xA1EE,0x24}, 
	{0xA1EF,0x03}, 
	{0xA1F0,0xF0}, 
	{0xA1F1,0x90}, 
	{0xA1F2,0x0A}, 
	{0xA1F3,0xBA}, 
	{0xA1F4,0xE0}, 
	{0xA1F5,0x34}, 
	{0xA1F6,0x00}, 
	{0xA1F7,0xF0}, 
	{0xA1F8,0x05}, 
	{0xA1F9,0x22}, 
	{0xA1FA,0x02}, 
	{0xA1FB,0xF1}, 
	{0xA1FC,0x41}, 
	{0xA1FD,0x90}, 
	{0xA1FE,0x0A}, 
	{0xA1FF,0xBA}, 
	{0xA200,0x74}, 
	{0xA201,0x0E}, 
	{0xA202,0xF0}, 
	{0xA203,0xA3}, 
	{0xA204,0x74}, 
	{0xA205,0xDC}, 
	{0xA206,0xF0}, 
	{0xA207,0xA3}, 
	{0xA208,0x74}, 
	{0xA209,0x05}, 
	{0xA20A,0xF0}, 
	{0xA20B,0xA3}, 
	{0xA20C,0x74}, 
	{0xA20D,0x61}, 
	{0xA20E,0xF0}, 
	{0xA20F,0x90}, 
	{0xA210,0x0A}, 
	{0xA211,0xBA}, 
	{0xA212,0xE0}, 
	{0xA213,0xFE}, 
	{0xA214,0xA3}, 
	{0xA215,0xE0}, 
	{0xA216,0xAA}, 
	{0xA217,0x06}, 
	{0xA218,0xF9}, 
	{0xA219,0x7B}, 
	{0xA21A,0x01}, 
	{0xA21B,0xC0}, 
	{0xA21C,0x02}, 
	{0xA21D,0xA3}, 
	{0xA21E,0xE0}, 
	{0xA21F,0xFE}, 
	{0xA220,0xA3}, 
	{0xA221,0xE0}, 
	{0xA222,0xAA}, 
	{0xA223,0x06}, 
	{0xA224,0xF8}, 
	{0xA225,0xAC}, 
	{0xA226,0x02}, 
	{0xA227,0x7D}, 
	{0xA228,0x01}, 
	{0xA229,0xD0}, 
	{0xA22A,0x02}, 
	{0xA22B,0x7E}, 
	{0xA22C,0x00}, 
	{0xA22D,0x7F}, 
	{0xA22E,0x04}, 
	{0xA22F,0x12}, 
	{0xA230,0x0F}, 
	{0xA231,0x6F}, 
	{0xA232,0x02}, 
	{0xA233,0x66}, 
	{0xA234,0xD9}, 
	{0xA235,0x90}, 
	{0xA236,0x07}, 
	{0xA237,0xD0}, 
	{0xA238,0x02}, 
	{0xA239,0xA2}, 
	{0xA23A,0x69}, 
	{0xA240,0x02}, 
	{0xA241,0x21}, 
	{0xA242,0x7F}, 
	{0xA243,0x02}, 
	{0xA244,0x21}, 
	{0xA245,0xF4}, 
	{0xA246,0x02}, 
	{0xA247,0xA6}, 
	{0xA248,0x15}, 
	{0xA249,0x60}, 
	{0xA24A,0x0A}, 
	{0xA24B,0xEF}, 
	{0xA24C,0xB4}, 
	{0xA24D,0x01}, 
	{0xA24E,0x16}, 
	{0xA24F,0x90}, 
	{0xA250,0x00}, 
	{0xA251,0x5D}, 
	{0xA252,0xE0}, 
	{0xA253,0x70}, 
	{0xA254,0x10}, 
	{0xA255,0x12}, 
	{0xA256,0x26}, 
	{0xA257,0xC8}, 
	{0xA258,0x90}, 
	{0xA259,0x00}, 
	{0xA25A,0x11}, 
	{0xA25B,0x74}, 
	{0xA25C,0x30}, 
	{0xA25D,0xF0}, 
	{0xA25E,0x90}, 
	{0xA25F,0x00}, 
	{0xA260,0x10}, 
	{0xA261,0x74}, 
	{0xA262,0x01}, 
	{0xA263,0xF0}, 
	{0xA264,0x22}, 
	{0xA265,0x12}, 
	{0xA266,0x25}, 
	{0xA267,0xA8}, 
	{0xA268,0x02}, 
	{0xA269,0x29}, 
	{0xA26A,0xFC}, 
	{0xA26B,0x44}, 
	{0xA26C,0x18}, 
	{0xA26D,0xF0}, 
	{0xA26E,0x90}, 
	{0xA26F,0x72}, 
	{0xA270,0x18}, 
	{0xA271,0xE0}, 
	{0xA272,0x44}, 
	{0xA273,0x18}, 
	{0xA274,0xF0}, 
	{0xA275,0x00}, 
	{0xA276,0x00}, 
	{0xA277,0x00}, 
	{0xA278,0x00}, 
	{0xA279,0x00}, 
	{0xA27A,0x00}, 
	{0xA27B,0x90}, 
	{0xA27C,0x72}, 
	{0xA27D,0x08}, 
	{0xA27E,0xE0}, 
	{0xA27F,0x44}, 
	{0xA280,0x10}, 
	{0xA281,0xF0}, 
	{0xA282,0x90}, 
	{0xA283,0x72}, 
	{0xA284,0x14}, 
	{0xA285,0xE0}, 
	{0xA286,0x54}, 
	{0xA287,0xFD}, 
	{0xA288,0xF0}, 
	{0xA289,0x22}, 
	{0xA29B,0xF0}, 
	{0xA29C,0xD3}, 
	{0xA29D,0x90}, 
	{0xA29E,0x07}, 
	{0xA29F,0x91}, 
	{0xA2A0,0xE0}, 
	{0xA2A1,0x94}, 
	{0xA2A2,0x21}, 
	{0xA2A3,0x90}, 
	{0xA2A4,0x07}, 
	{0xA2A5,0x90}, 
	{0xA2A6,0xE0}, 
	{0xA2A7,0x64}, 
	{0xA2A8,0x80}, 
	{0xA2A9,0x94}, 
	{0xA2AA,0x81}, 
	{0xA2AB,0x40}, 
	{0xA2AC,0x08}, 
	{0xA2AD,0x90}, 
	{0xA2AE,0x07}, 
	{0xA2AF,0xCB}, 
	{0xA2B0,0x74}, 
	{0xA2B1,0xFF}, 
	{0xA2B2,0xF0}, 
	{0xA2B3,0x80}, 
	{0xA2B4,0x06}, 
	{0xA2B5,0x90}, 
	{0xA2B6,0x07}, 
	{0xA2B7,0xCB}, 
	{0xA2B8,0x74}, 
	{0xA2B9,0x01}, 
	{0xA2BA,0xF0}, 
	{0xA2BB,0x02}, 
	{0xA2BC,0xB5}, 
	{0xA2BD,0xC3}, 
	{0xA2BE,0x90}, 
	{0xA2BF,0x08}, 
	{0xA2C0,0x34}, 
	{0xA2C1,0xE0}, 
	{0xA2C2,0xFC}, 
	{0xA2C3,0xA3}, 
	{0xA2C4,0xE0}, 
	{0xA2C5,0xFD}, 
	{0xA2C6,0xA3}, 
	{0xA2C7,0xE0}, 
	{0xA2C8,0xFE}, 
	{0xA2C9,0xA3}, 
	{0xA2CA,0xE0}, 
	{0xA2CB,0xFF}, 
	{0xA2CC,0x90}, 
	{0xA2CD,0x07}, 
	{0xA2CE,0xD0}, 
	{0xA2CF,0xE0}, 
	{0xA2D0,0xF8}, 
	{0xA2D1,0xA3}, 
	{0xA2D2,0xE0}, 
	{0xA2D3,0xF9}, 
	{0xA2D4,0xA3}, 
	{0xA2D5,0xE0}, 
	{0xA2D6,0xFA}, 
	{0xA2D7,0xA3}, 
	{0xA2D8,0xE0}, 
	{0xA2D9,0xFB}, 
	{0xA2DA,0xD3}, 
	{0xA2DB,0x12}, 
	{0xA2DC,0x0D}, 
	{0xA2DD,0xAE}, 
	{0xA2DE,0x40}, 
	{0xA2DF,0x0B}, 
	{0xA2E0,0x12}, 
	{0xA2E1,0xB5}, 
	{0xA2E2,0x49}, 
	{0xA2E3,0x90}, 
	{0xA2E4,0x07}, 
	{0xA2E5,0xA4}, 
	{0xA2E6,0x74}, 
	{0xA2E7,0x02}, 
	{0xA2E8,0xF0}, 
	{0xA2E9,0x80}, 
	{0xA2EA,0x09}, 
	{0xA2EB,0x12}, 
	{0xA2EC,0xB7}, 
	{0xA2ED,0x51}, 
	{0xA2EE,0x90}, 
	{0xA2EF,0x07}, 
	{0xA2F0,0xA4}, 
	{0xA2F1,0x74}, 
	{0xA2F2,0x05}, 
	{0xA2F3,0xF0}, 
	{0xA2F4,0x02}, 
	{0xA2F5,0xA2}, 
	{0xA2F6,0xDA}, 
	{0xA2F7,0x90}, 
	{0xA2F8,0x0E}, 
	{0xA2F9,0xE0}, 
	{0xA2FA,0xE0}, 
	{0xA2FB,0xFD}, 
	{0xA2FC,0xA3}, 
	{0xA2FD,0xE0}, 
	{0xA2FE,0x90}, 
	{0xA2FF,0x02}, 
	{0xA300,0xA2}, 
	{0xA301,0xCD}, 
	{0xA302,0xF0}, 
	{0xA303,0xA3}, 
	{0xA304,0xED}, 
	{0xA305,0xF0}, 
	{0xA306,0x90}, 
	{0xA307,0x0E}, 
	{0xA308,0xE2}, 
	{0xA309,0xE0}, 
	{0xA30A,0xFD}, 
	{0xA30B,0xA3}, 
	{0xA30C,0xE0}, 
	{0xA30D,0x90}, 
	{0xA30E,0x02}, 
	{0xA30F,0xA8}, 
	{0xA310,0xCD}, 
	{0xA311,0xF0}, 
	{0xA312,0xA3}, 
	{0xA313,0xED}, 
	{0xA314,0xF0}, 
	{0xA315,0xE4}, 
	{0xA316,0x90}, 
	{0xA317,0x06}, 
	{0xA318,0x38}, 
	{0xA319,0xF0}, 
	{0xA31A,0x02}, 
	{0xA31B,0x67}, 
	{0xA31C,0x63}, 
	{0xA31D,0x90}, 
	{0xA31E,0x0E}, 
	{0xA31F,0xE8}, 
	{0xA320,0xE0}, 
	{0xA321,0x90}, 
	{0xA322,0x02}, 
	{0xA323,0x62}, 
	{0xA324,0xF0}, 
	{0xA325,0x90}, 
	{0xA326,0x0E}, 
	{0xA327,0xE9}, 
	{0xA328,0xE0}, 
	{0xA329,0x90}, 
	{0xA32A,0x02}, 
	{0xA32B,0x63}, 
	{0xA32C,0xF0}, 
	{0xA32D,0x02}, 
	{0xA32E,0x67}, 
	{0xA32F,0x1F}, 
	{0xA33B,0x90}, 
	{0xA33C,0x0E}, 
	{0xA33D,0x14}, 
	{0xA33E,0xE0}, 
	{0xA33F,0xFE}, 
	{0xA340,0xA3}, 
	{0xA341,0xE0}, 
	{0xA342,0xFF}, 
	{0xA343,0x90}, 
	{0xA344,0x06}, 
	{0xA345,0xD9}, 
	{0xA346,0xEE}, 
	{0xA347,0xF0}, 
	{0xA348,0xA3}, 
	{0xA349,0xEF}, 
	{0xA34A,0xF0}, 
	{0xA34B,0x90}, 
	{0xA34C,0x0E}, 
	{0xA34D,0x18}, 
	{0xA34E,0xE0}, 
	{0xA34F,0xFD}, 
	{0xA350,0x7C}, 
	{0xA351,0x00}, 
	{0xA352,0xC3}, 
	{0xA353,0xEF}, 
	{0xA354,0x9D}, 
	{0xA355,0xEE}, 
	{0xA356,0x9C}, 
	{0xA357,0x50}, 
	{0xA358,0x09}, 
	{0xA359,0xE4}, 
	{0xA35A,0x90}, 
	{0xA35B,0x06}, 
	{0xA35C,0xD7}, 
	{0xA35D,0xF0}, 
	{0xA35E,0xA3}, 
	{0xA35F,0xF0}, 
	{0xA360,0x80}, 
	{0xA361,0x13}, 
	{0xA362,0xC3}, 
	{0xA363,0x90}, 
	{0xA364,0x06}, 
	{0xA365,0xDA}, 
	{0xA366,0xE0}, 
	{0xA367,0x9D}, 
	{0xA368,0xFE}, 
	{0xA369,0x90}, 
	{0xA36A,0x06}, 
	{0xA36B,0xD9}, 
	{0xA36C,0xE0}, 
	{0xA36D,0x9C}, 
	{0xA36E,0x90}, 
	{0xA36F,0x06}, 
	{0xA370,0xD7}, 
	{0xA371,0xF0}, 
	{0xA372,0xA3}, 
	{0xA373,0xCE}, 
	{0xA374,0xF0}, 
	{0xA375,0x90}, 
	{0xA376,0x0E}, 
	{0xA377,0x18}, 
	{0xA378,0xE0}, 
	{0xA379,0xF9}, 
	{0xA37A,0xFF}, 
	{0xA37B,0x90}, 
	{0xA37C,0x06}, 
	{0xA37D,0xC2}, 
	{0xA37E,0xE0}, 
	{0xA37F,0xFC}, 
	{0xA380,0xA3}, 
	{0xA381,0xE0}, 
	{0xA382,0xFD}, 
	{0xA383,0xC3}, 
	{0xA384,0x9F}, 
	{0xA385,0xFF}, 
	{0xA386,0xEC}, 
	{0xA387,0x94}, 
	{0xA388,0x00}, 
	{0xA389,0xFE}, 
	{0xA38A,0x90}, 
	{0xA38B,0x0E}, 
	{0xA38C,0x16}, 
	{0xA38D,0xE0}, 
	{0xA38E,0xFA}, 
	{0xA38F,0xA3}, 
	{0xA390,0xE0}, 
	{0xA391,0xFB}, 
	{0xA392,0xD3}, 
	{0xA393,0x9F}, 
	{0xA394,0xEA}, 
	{0xA395,0x9E}, 
	{0xA396,0x40}, 
	{0xA397,0x0A}, 
	{0xA398,0x90}, 
	{0xA399,0x06}, 
	{0xA39A,0xD5}, 
	{0xA39B,0xEC}, 
	{0xA39C,0xF0}, 
	{0xA39D,0xA3}, 
	{0xA39E,0xED}, 
	{0xA39F,0xF0}, 
	{0xA3A0,0x80}, 
	{0xA3A1,0x0E}, 
	{0xA3A2,0xE9}, 
	{0xA3A3,0x7E}, 
	{0xA3A4,0x00}, 
	{0xA3A5,0x2B}, 
	{0xA3A6,0xFF}, 
	{0xA3A7,0xEE}, 
	{0xA3A8,0x3A}, 
	{0xA3A9,0x90}, 
	{0xA3AA,0x06}, 
	{0xA3AB,0xD5}, 
	{0xA3AC,0xF0}, 
	{0xA3AD,0xA3}, 
	{0xA3AE,0xEF}, 
	{0xA3AF,0xF0}, 
	{0xA3B0,0xE9}, 
	{0xA3B1,0xFB}, 
	{0xA3B2,0x7A}, 
	{0xA3B3,0x00}, 
	{0xA3B4,0x90}, 
	{0xA3B5,0x0E}, 
	{0xA3B6,0x15}, 
	{0xA3B7,0xE0}, 
	{0xA3B8,0x2B}, 
	{0xA3B9,0xFE}, 
	{0xA3BA,0x90}, 
	{0xA3BB,0x0E}, 
	{0xA3BC,0x14}, 
	{0xA3BD,0xE0}, 
	{0xA3BE,0x3A}, 
	{0xA3BF,0x90}, 
	{0xA3C0,0x06}, 
	{0xA3C1,0xE1}, 
	{0xA3C2,0xF0}, 
	{0xA3C3,0xA3}, 
	{0xA3C4,0xCE}, 
	{0xA3C5,0xF0}, 
	{0xA3C6,0xC3}, 
	{0xA3C7,0x90}, 
	{0xA3C8,0x0E}, 
	{0xA3C9,0x17}, 
	{0xA3CA,0xE0}, 
	{0xA3CB,0x9B}, 
	{0xA3CC,0xFE}, 
	{0xA3CD,0x90}, 
	{0xA3CE,0x0E}, 
	{0xA3CF,0x16}, 
	{0xA3D0,0x02}, 
	{0xA3D1,0x20}, 
	{0xA3D2,0xD5}, 
	{0xA3D3,0x90}, 
	{0xA3d4,0x0E}, 
	{0xA3d5,0xE4}, 
	{0xA3d6,0xE0}, 
	{0xA3d7,0x90}, 
	{0xA3d8,0x02}, 
	{0xA3d9,0x66}, 
	{0xA3da,0xF0}, 
	{0xA3DB,0x90}, 
	{0xA3dc,0x0E}, 
	{0xA3dd,0xE5}, 
	{0xA3de,0xE0}, 
	{0xA3df,0x90}, 
	{0xA3e0,0x02}, 
	{0xA3e1,0x64}, 
	{0xA3e2,0xF0}, 
	{0xA3e3,0x90}, 
	{0xA3e4,0x0E}, 
	{0xA3e5,0xE6}, 
	{0xA3e6,0xE0}, 
	{0xA3e7,0x90}, 
	{0xA3e8,0x02}, 
	{0xA3e9,0x65}, 
	{0xA3ea,0xF0}, 
	{0xA3eb,0x02}, 
	{0xA3ec,0x67}, 
	{0xA3ed,0xA5}, 
	{0xA3f0,0x12}, 
	{0xA3f1,0x47}, 
	{0xA3f2,0x59}, 
	{0xA3f3,0x90}, 
	{0xA3f4,0x00}, 
	{0xA3f5,0xB5}, 
	{0xA3f6,0xE0}, 
	{0xA3f7,0xB4}, 
	{0xA3f8,0x02}, 
	{0xA3f9,0x03}, 
	{0xA3fa,0x12}, 
	{0xA3fb,0x47}, 
	{0xA3fc,0x59}, 
	{0xA3fd,0x02}, 
	{0xA3fe,0xC5}, 
	{0xA3ff,0xC3}, 
	{0xA400,0x90}, 
	{0xA401,0x00}, 
	{0xA402,0x3D}, 
	{0xA403,0xF0}, 
	{0xA404,0x90}, 
	{0xA405,0x00}, 
	{0xA406,0x84}, 
	{0xA407,0xE0}, 
	{0xA408,0xFE}, 
	{0xA409,0x90}, 
	{0xA40A,0x00}, 
	{0xA40B,0x3E}, 
	{0xA40C,0xF0}, 
	{0xA40D,0xEF}, 
	{0xA40E,0x70}, 
	{0xA40F,0x03}, 
	{0xA410,0xEE}, 
	{0xA411,0x60}, 
	{0xA412,0x04}, 
	{0xA413,0x7F}, 
	{0xA414,0x01}, 
	{0xA415,0x80}, 
	{0xA416,0x02}, 
	{0xA417,0x7F}, 
	{0xA418,0x00}, 
	{0xA419,0x90}, 
	{0xA41A,0x00}, 
	{0xA41B,0x3F}, 
	{0xA41C,0xEF}, 
	{0xA41D,0xF0}, 
	{0xA41E,0x02}, 
	{0xA41F,0x89}, 
	{0xA420,0xD3}, 
	{0xA421,0x90}, 
	{0xA422,0x00}, 
	{0xA423,0x12}, 
	{0xA424,0xE0}, 
	{0xA425,0xFF}, 
	{0xA426,0x70}, 
	{0xA427,0x0C}, 
	{0xA428,0x90}, 
	{0xA429,0x00}, 
	{0xA42A,0x46}, 
	{0xA42B,0xE0}, 
	{0xA42C,0xC3}, 
	{0xA42D,0x94}, 
	{0xA42E,0x07}, 
	{0xA42F,0x40}, 
	{0xA430,0x03}, 
	{0xA431,0x75}, 
	{0xA432,0x2E}, 
	{0xA433,0x02}, 
	{0xA434,0xEF}, 
	{0xA435,0xB4}, 
	{0xA436,0x01}, 
	{0xA437,0x0C}, 
	{0xA438,0x90}, 
	{0xA439,0x00}, 
	{0xA43A,0x66}, 
	{0xA43B,0xE0}, 
	{0xA43C,0xC3}, 
	{0xA43D,0x94}, 
	{0xA43E,0x07}, 
	{0xA43F,0x40}, 
	{0xA440,0x03}, 
	{0xA441,0x75}, 
	{0xA442,0x2E}, 
	{0xA443,0x02}, 
	{0xA444,0x02}, 
	{0xA445,0xA7}, 
	{0xA446,0x9E}, 
	{0xA447,0xC3}, 
	{0xA448,0x90}, 
	{0xA449,0x0B}, 
	{0xA44A,0x8F}, 
	{0xA44B,0xE0}, 
	{0xA44C,0x94}, 
	{0xA44D,0x00}, 
	{0xA44E,0x90}, 
	{0xA44F,0x0B}, 
	{0xA450,0x8E}, 
	{0xA451,0xE0}, 
	{0xA452,0x94}, 
	{0xA453,0x41}, 
	{0xA454,0x40}, 
	{0xA455,0x22}, 
	{0xA456,0x90}, 
	{0xA457,0x0B}, 
	{0xA458,0x91}, 
	{0xA459,0xE0}, 
	{0xA45A,0x94}, 
	{0xA45B,0x00}, 
	{0xA45C,0x90}, 
	{0xA45D,0x0B}, 
	{0xA45E,0x90}, 
	{0xA45F,0xE0}, 
	{0xA460,0x94}, 
	{0xA461,0x41}, 
	{0xA462,0x40}, 
	{0xA463,0x14}, 
	{0xA464,0x90}, 
	{0xA465,0x0B}, 
	{0xA466,0x93}, 
	{0xA467,0xE0}, 
	{0xA468,0x94}, 
	{0xA469,0x00}, 
	{0xA46A,0x90}, 
	{0xA46B,0x0B}, 
	{0xA46C,0x92}, 
	{0xA46D,0xE0}, 
	{0xA46E,0x94}, 
	{0xA46F,0x41}, 
	{0xA470,0x40}, 
	{0xA471,0x06}, 
	{0xA472,0x90}, 
	{0xA473,0x01}, 
	{0xA474,0xA4}, 
	{0xA475,0x02}, 
	{0xA476,0x86}, 
	{0xA477,0x57}, 
	{0xA478,0x02}, 
	{0xA479,0x86}, 
	{0xA47A,0x5C}, 
	{0xA500,0xF5}, 
	{0xA501,0x3B}, 
	{0xA502,0x90}, 
	{0xA503,0x06}, 
	{0xA504,0x6C}, 
	{0xA505,0xE0}, 
	{0xA506,0xFF}, 
	{0xA507,0xE5}, 
	{0xA508,0x3B}, 
	{0xA509,0xC3}, 
	{0xA50A,0x9F}, 
	{0xA50B,0x40}, 
	{0xA50C,0x03}, 
	{0xA50D,0x02}, 
	{0xA50E,0xF6}, 
	{0xA50F,0x0E}, 
	{0xA510,0x90}, 
	{0xA511,0x0B}, 
	{0xA512,0xC6}, 
	{0xA513,0xE0}, 
	{0xA514,0x14}, 
	{0xA515,0x60}, 
	{0xA516,0x3C}, 
	{0xA517,0x14}, 
	{0xA518,0x60}, 
	{0xA519,0x6B}, 
	{0xA51A,0x24}, 
	{0xA51B,0x02}, 
	{0xA51C,0x60}, 
	{0xA51D,0x03}, 
	{0xA51E,0x02}, 
	{0xA51F,0xF5}, 
	{0xA520,0xB5}, 
	{0xA521,0x90}, 
	{0xA522,0x0A}, 
	{0xA523,0x9A}, 
	{0xA524,0xE0}, 
	{0xA525,0xFB}, 
	{0xA526,0xA3}, 
	{0xA527,0xE0}, 
	{0xA528,0xFA}, 
	{0xA529,0xA3}, 
	{0xA52A,0xE0}, 
	{0xA52B,0xF9}, 
	{0xA52C,0x85}, 
	{0xA52D,0x3B}, 
	{0xA52E,0x82}, 
	{0xA52F,0x75}, 
	{0xA530,0x83}, 
	{0xA531,0x00}, 
	{0xA532,0x12}, 
	{0xA533,0x0A}, 
	{0xA534,0xB8}, 
	{0xA535,0xFF}, 
	{0xA536,0x74}, 
	{0xA537,0xAB}, 
	{0xA538,0x25}, 
	{0xA539,0x3B}, 
	{0xA53A,0xF5}, 
	{0xA53B,0x82}, 
	{0xA53C,0xE4}, 
	{0xA53D,0x34}, 
	{0xA53E,0x0A}, 
	{0xA53F,0xF5}, 
	{0xA540,0x83}, 
	{0xA541,0xE0}, 
	{0xA542,0xFD}, 
	{0xA543,0xC3}, 
	{0xA544,0xEF}, 
	{0xA545,0x9D}, 
	{0xA546,0xFE}, 
	{0xA547,0xE4}, 
	{0xA548,0x94}, 
	{0xA549,0x00}, 
	{0xA54A,0x90}, 
	{0xA54B,0x0B}, 
	{0xA54C,0xCA}, 
	{0xA54D,0xF0}, 
	{0xA54E,0xA3}, 
	{0xA54F,0xCE}, 
	{0xA550,0xF0}, 
	{0xA551,0x80}, 
	{0xA552,0x62}, 
	{0xA553,0x90}, 
	{0xA554,0x0A}, 
	{0xA555,0x9A}, 
	{0xA556,0xE0}, 
	{0xA557,0xFB}, 
	{0xA558,0xA3}, 
	{0xA559,0xE0}, 
	{0xA55A,0xFA}, 
	{0xA55B,0xA3}, 
	{0xA55C,0xE0}, 
	{0xA55D,0xF9}, 
	{0xA55E,0x85}, 
	{0xA55F,0x3B}, 
	{0xA560,0x82}, 
	{0xA561,0x75}, 
	{0xA562,0x83}, 
	{0xA563,0x00}, 
	{0xA564,0x12}, 
	{0xA565,0x0A}, 
	{0xA566,0xB8}, 
	{0xA567,0xFF}, 
	{0xA568,0x74}, 
	{0xA569,0x9D}, 
	{0xA56A,0x25}, 
	{0xA56B,0x3B}, 
	{0xA56C,0xF5}, 
	{0xA56D,0x82}, 
	{0xA56E,0xE4}, 
	{0xA56F,0x34}, 
	{0xA570,0x0A}, 
	{0xA571,0xF5}, 
	{0xA572,0x83}, 
	{0xA573,0xE0}, 
	{0xA574,0xFD}, 
	{0xA575,0xC3}, 
	{0xA576,0xEF}, 
	{0xA577,0x9D}, 
	{0xA578,0xFE}, 
	{0xA579,0xE4}, 
	{0xA57A,0x94}, 
	{0xA57B,0x00}, 
	{0xA57C,0x90}, 
	{0xA57D,0x0B}, 
	{0xA57E,0xCA}, 
	{0xA57F,0xF0}, 
	{0xA580,0xA3}, 
	{0xA581,0xCE}, 
	{0xA582,0xF0}, 
	{0xA583,0x80}, 
	{0xA584,0x30}, 
	{0xA585,0x90}, 
	{0xA586,0x0A}, 
	{0xA587,0x9A}, 
	{0xA588,0xE0}, 
	{0xA589,0xFB}, 
	{0xA58A,0xA3}, 
	{0xA58B,0xE0}, 
	{0xA58C,0xFA}, 
	{0xA58D,0xA3}, 
	{0xA58E,0xE0}, 
	{0xA58F,0xF9}, 
	{0xA590,0x85}, 
	{0xA591,0x3B}, 
	{0xA592,0x82}, 
	{0xA593,0x75}, 
	{0xA594,0x83}, 
	{0xA595,0x00}, 
	{0xA596,0x12}, 
	{0xA597,0x0A}, 
	{0xA598,0xB8}, 
	{0xA599,0xFF}, 
	{0xA59A,0x74}, 
	{0xA59B,0xA4}, 
	{0xA59C,0x25}, 
	{0xA59D,0x3B}, 
	{0xA59E,0xF5}, 
	{0xA59F,0x82}, 
	{0xA5A0,0xE4}, 
	{0xA5A1,0x34}, 
	{0xA5A2,0x0A}, 
	{0xA5A3,0xF5}, 
	{0xA5A4,0x83}, 
	{0xA5A5,0xE0}, 
	{0xA5A6,0xFD}, 
	{0xA5A7,0xC3}, 
	{0xA5A8,0xEF}, 
	{0xA5A9,0x9D}, 
	{0xA5AA,0xFE}, 
	{0xA5AB,0xE4}, 
	{0xA5AC,0x94}, 
	{0xA5AD,0x00}, 
	{0xA5AE,0x90}, 
	{0xA5AF,0x0B}, 
	{0xA5B0,0xCA}, 
	{0xA5B1,0xF0}, 
	{0xA5B2,0xA3}, 
	{0xA5B3,0xCE}, 
	{0xA5B4,0xF0}, 
	{0xA5B5,0x90}, 
	{0xA5B6,0x07}, 
	{0xA5B7,0x83}, 
	{0xA5B8,0xE0}, 
	{0xA5B9,0xFF}, 
	{0xA5BA,0x7E}, 
	{0xA5BB,0x00}, 
	{0xA5BC,0x90}, 
	{0xA5BD,0x0D}, 
	{0xA5BE,0xF6}, 
	{0xA5BF,0xEE}, 
	{0xA5C0,0xF0}, 
	{0xA5C1,0xA3}, 
	{0xA5C2,0xEF}, 
	{0xA5C3,0xF0}, 
	{0xA5C4,0x90}, 
	{0xA5C5,0x0B}, 
	{0xA5C6,0xCA}, 
	{0xA5C7,0xE0}, 
	{0xA5C8,0xFC}, 
	{0xA5C9,0xA3}, 
	{0xA5CA,0xE0}, 
	{0xA5CB,0xFD}, 
	{0xA5CC,0xD3}, 
	{0xA5CD,0x9F}, 
	{0xA5CE,0x74}, 
	{0xA5CF,0x80}, 
	{0xA5D0,0xF8}, 
	{0xA5D1,0xEC}, 
	{0xA5D2,0x64}, 
	{0xA5D3,0x80}, 
	{0xA5D4,0x98}, 
	{0xA5D5,0x40}, 
	{0xA5D6,0x0C}, 
	{0xA5D7,0x90}, 
	{0xA5D8,0x0B}, 
	{0xA5D9,0xC8}, 
	{0xA5DA,0xE0}, 
	{0xA5DB,0x04}, 
	{0xA5DC,0xF0}, 
	{0xA5DD,0xA3}, 
	{0xA5DE,0xE0}, 
	{0xA5DF,0x04}, 
	{0xA5E0,0xF0}, 
	{0xA5E1,0x80}, 
	{0xA5E2,0x26}, 
	{0xA5E3,0x90}, 
	{0xA5E4,0x0D}, 
	{0xA5E5,0xF6}, 
	{0xA5E6,0xE0}, 
	{0xA5E7,0xFE}, 
	{0xA5E8,0xA3}, 
	{0xA5E9,0xE0}, 
	{0xA5EA,0xFF}, 
	{0xA5EB,0xC3}, 
	{0xA5EC,0xE4}, 
	{0xA5ED,0x9F}, 
	{0xA5EE,0xFF}, 
	{0xA5EF,0xE4}, 
	{0xA5F0,0x9E}, 
	{0xA5F1,0xFE}, 
	{0xA5F2,0xC3}, 
	{0xA5F3,0xED}, 
	{0xA5F4,0x9F}, 
	{0xA5F5,0xEE}, 
	{0xA5F6,0x64}, 
	{0xA5F7,0x80}, 
	{0xA5F8,0xF8}, 
	{0xA5F9,0xEC}, 
	{0xA5FA,0x64}, 
	{0xA5FB,0x80}, 
	{0xA5FC,0x98}, 
	{0xA5FD,0x50}, 
	{0xA5FE,0x0A}, 
	{0xA5FF,0x90}, 
	{0xA600,0x0B}, 
	{0xA601,0xC8}, 
	{0xA602,0xE0}, 
	{0xA603,0x14}, 
	{0xA604,0xF0}, 
	{0xA605,0xA3}, 
	{0xA606,0xE0}, 
	{0xA607,0x04}, 
	{0xA608,0xF0}, 
	{0xA609,0x05}, 
	{0xA60A,0x3B}, 
	{0xA60B,0x02}, 
	{0xA60C,0xF5}, 
	{0xA60D,0x02}, 
	{0xA60E,0x90}, 
	{0xA60F,0x08}, 
	{0xA610,0x58}, 
	{0xA611,0x02}, 
	{0xA612,0x9D}, 
	{0xA613,0x50}, 
	{0x9006,0xBA}, 
	{0x9007,0x75}, 
	{0x9008,0x00}, 
	{0x9009,0x00}, 
	{0x900A,0x02}, 
	{0x900D,0x01}, 
	{0x900E,0xA2}, 
	{0x900F,0x8F}, 
	{0x9010,0x00}, 
	{0x9011,0xCB}, 
	{0x9012,0x03}, 
	{0x9016,0xE6}, 
	{0x9017,0x6B}, 
	{0x9018,0x02}, 
	{0x9019,0x6B}, 
	{0x901A,0x02}, 
	{0x901D,0x01}, 
	{0x901E,0xAC}, 
	{0x901F,0x70}, 
	{0x9020,0x00}, 
	{0x9021,0xC5}, 
	{0x9022,0x03}, 
	{0x9026,0x9C}, 
	{0x9027,0x5B}, 
	{0x9028,0x00}, 
	{0x9029,0xBF}, 
	{0x902A,0x02}, 
	{0x902E,0x60}, 
	{0x902F,0x1C}, 
	{0x9030,0x01}, 
	{0x9031,0x37}, 
	{0x9032,0x02}, 
	{0x9035,0x01}, 
	{0x9036,0xBA}, 
	{0x9037,0x70}, 
	{0x9038,0x00}, 
	{0x9039,0x00}, 
	{0x903A,0x03}, 
	{0x903E,0x21}, 
	{0x903F,0x3F}, 
	{0x9040,0x02}, 
	{0x9041,0x40}, 
	{0x9042,0x02}, 
	{0x9046,0x21}, 
	{0x9047,0xEA}, 
	{0x9048,0x02}, 
	{0x9049,0x43}, 
	{0x904A,0x02}, 
	{0x904E,0xA6}, 
	{0x904F,0x12}, 
	{0x9050,0x02}, 
	{0x9051,0x46}, 
	{0x9052,0x02}, 
	{0x9056,0x29}, 
	{0x9057,0xE3}, 
	{0x9058,0x02}, 
	{0x9059,0x49}, 
	{0x905A,0x02}, 
	{0x905D,0x01}, 
	{0x905E,0x9C}, 
	{0x905F,0x6E}, 
	{0x9060,0x05}, 
	{0x9061,0x00}, 
	{0x9062,0x02}, 
	{0x9065,0x01}, 
	{0x9066,0xA2}, 
	{0x9067,0x66}, 
	{0x9068,0x02}, 
	{0x9069,0x35}, 
	{0x906A,0x02}, 
	{0x906D,0x01}, 
	{0x906E,0xB5}, 
	{0x906F,0xC2}, 
	{0x9070,0x02}, 
	{0x9071,0x9B}, 
	{0x9072,0x02}, 
	{0x9075,0x01}, 
	{0x9076,0xA2}, 
	{0x9077,0xD4}, 
	{0x9078,0x02}, 
	{0x9079,0xBE}, 
	{0x907A,0x02}, 
	{0x907D,0x01}, 
	{0x907E,0xB7}, 
	{0x907F,0xEA}, 
	{0x9080,0x00}, 
	{0x9081,0x02}, 
	{0x9082,0x03}, 
	{0x9086,0x67}, 
	{0x9087,0x31}, 
	{0x9088,0x02}, 
	{0x9089,0xF7}, 
	{0x908A,0x02}, 
	{0x908E,0x66}, 
	{0x908F,0xED}, 
	{0x9090,0x03}, 
	{0x9091,0x1D}, 
	{0x9092,0x02}, 
	{0x9096,0x67}, 
	{0x9097,0x73}, 
	{0x9098,0x03}, 
	{0x9099,0xD3}, 
	{0x909A,0x02}, 
	{0x909E,0x20}, 
	{0x909F,0x40}, 
	{0x90A0,0x03}, 
	{0x90A1,0x3B}, 
	{0x90A2,0x02}, 
	{0x90A6,0xC5}, 
	{0x90A7,0xC0}, 
	{0x90A8,0x03}, 
	{0x90A9,0xF0}, 
	{0x90AA,0x02}, 
	{0x90AE,0x41}, 
	{0x90AF,0xB3}, 
	{0x90B0,0x00}, 
	{0x90B1,0xA2}, 
	{0x90B2,0x02}, 
	{0x90B6,0x44}, 
	{0x90B7,0xBA}, 
	{0x90B8,0x00}, 
	{0x90B9,0xF0}, 
	{0x90BA,0x03}, 
	{0x90BE,0x89}, 
	{0x90BF,0x99}, 
	{0x90C0,0x04}, 
	{0x90C1,0x00}, 
	{0x90C2,0x02}, 
	{0x90C6,0xA7}, 
	{0x90C7,0x91}, 
	{0x90C8,0x04}, 
	{0x90C9,0x21}, 
	{0x90CA,0x02}, 
	{0x90CE,0x3A}, 
	{0x90CF,0x51}, 
	{0x90D0,0x00}, 
	{0x90D1,0xA2}, 
	{0x90D2,0x02}, 
	{0x90D6,0x86}, 
	{0x90D7,0x54}, 
	{0x90D8,0x04}, 
	{0x90D9,0x47}, 
	{0x90DA,0x02}, 
	{0x9000,0x01}, 
	{0xffff,0x00},   // 2013-03-01 end
	{SEQUENCE_END, 0x00}
#endif 
};
static struct reginfo sensor_init_data1[] =
{
{0x0009,0x16}, //10
{0x0085,0x00}, 
{0x0040,0x00},
{0x0016,0x00},
{0x0046,0x00},
{0x0041,0x00},
{0x00B5,0x02},
{0x7101,0x44},
{0x00ED,0x0A},
{0x00EE,0x1E},
{0x00B3,0x80},
{0x7104,0x00},
{0x7105,0x80},
{0x019C,0x4B},
{0x019D,0x20},
{0x0129,0x00},
{0x0130,0x00},
{0x0083,0x01},
{0x0084,0x01},
{0x01A1,0x80},
{0x01A2,0x80},
{0x01A3,0x80},
{0x01A0,0x01},
{0x0021,0x00},
{0x0022,0x01},

{0x0040,0x00}, //00
{0x0060,0x00},
{0x0013,0x00},
{0x0041,0x00},//00:5M 03:SVGA

{0x0061,0x00},
{0x0046,0x02},
{0x0066,0x02},
{0x0012,0x00},
{0x7102,0x09},
{0x7103,0x00},
{0x7158,0x00},
{0x00E8,0x01},
{0x7000,0x2C},
{0x5200,0x01},
{0x7000,0x0C},
{0x0143,0x5F},
{0x0144,0x0D},
{0x02C2,0x00},
{0x02C3,0xC0},
{0x015E,0x40},
{0x015F,0x00},
{0x0390,0x01},
{0x0391,0x00},
{0x0392,0x00},
{0x03A0,0x14},
{0x03A1,0x00},
{0x03A2,0x5A},
{0x03A3,0xEE},
{0x03A4,0x69},
{0x03A5,0x49},
{0x03A6,0x3E},
{0x03A7,0x00},
{0x03A8,0x39},
{0x03A9,0x33},
{0x03B0,0x60},
{0x03B1,0x00},
{0x03B2,0x5A},
{0x03B3,0xEE},
{0x03B4,0x69},
{0x03B5,0x49},
{0x03B6,0x3E},
{0x03B7,0x00},
{0x03B8,0x3D},
{0x03B9,0x20},
{0x03C0,0x10},
{0x03C1,0x00},
{0x03C2,0x5A},
{0x03C3,0xEE},
{0x03C4,0x69},
{0x03C5,0x49},
{0x03C6,0x3A},
{0x03C7,0x80},
{0x03D0,0x64},
{0x03D1,0x00},
{0x03D2,0x5A},
{0x03D3,0xEE},
{0x03D4,0x69},
{0x03D5,0x49},
{0x03D6,0x34},
{0x03D7,0xD1},
{0x004C,0x08},
{0x006C,0x08},
{0x0350,0x00},
{0x0351,0x5A},
{0x0352,0xEE},
{0x0353,0x69},
{0x0354,0x49},
{0x0355,0x39},
{0x0356,0x6D},
{0x0357,0x19},
{0x0358,0x00},
{0x0359,0x3C},
{0x035A,0x5A},
{0x035B,0xEE},
{0x035C,0x69},
{0x035D,0x49},
{0x035E,0x39},
{0x035F,0x85},
{0x0049,0x14},
{0x004A,0x0D},
{0x0069,0x14},
{0x006A,0x0D},
{0x0090,0x00},
{0x0091,0x5A},
{0x0092,0xEE},
{0x0093,0x3E},
{0x0094,0x00},
{0x0095,0x69},
{0x0096,0x49},
{0x0097,0x39},
{0x0098,0xCF},
{0x00A0,0x00},
{0x00A1,0x5A},
{0x00A2,0xEE},
{0x00A3,0x3E},
{0x00A4,0x00},
{0x00A5,0x69},
{0x00A6,0x49},
{0x00A7,0x3B},
{0x00A8,0x80},
#if 0 // 03-06 old LSC
{0x0420,0x00},
{0x0421,0x09},
{0x0422,0xff},
{0x0423,0x9e},
{0x0424,0x00},
{0x0425,0x89},
{0x0426,0x00},
{0x0427,0xab},
{0x0428,0xff},
{0x0429,0xe9},
{0x042a,0xff},
{0x042b,0x8b},
{0x042c,0x00},
{0x042d,0x73},
{0x042e,0xff},
{0x042f,0xb6},
{0x0430,0x00},
{0x0431,0x54},
{0x0432,0xff},
{0x0433,0x43},
{0x0434,0x01},
{0x0435,0x04},
{0x0436,0x01},
{0x0437,0x34},
{0x0438,0xff},
{0x0439,0x7c},
{0x043a,0xfe},
{0x043b,0xd2},
{0x043c,0x00},
{0x043d,0x63},
{0x043e,0xff},
{0x043f,0x15},
{0x0450,0x00},
{0x0451,0x3b},
{0x0452,0xff},
{0x0453,0x98},
{0x0454,0x00},
{0x0455,0x6f},
{0x0456,0x00},
{0x0457,0x93},
{0x0458,0xff},
{0x0459,0xad},
{0x045a,0xff},
{0x045b,0x87},
{0x045c,0x00},
{0x045d,0x52},
{0x045e,0xff},
{0x045f,0xa7},
{0x0440,0xff},
{0x0441,0xfd},
{0x0442,0xff},
{0x0443,0x6c},
{0x0444,0x00},
{0x0445,0x90},
{0x0446,0x00},
{0x0447,0xa1},
{0x0448,0x00},
{0x0449,0x02},
{0x044a,0xff},
{0x044b,0x48},
{0x044c,0x00},
{0x044d,0x5b},
{0x044e,0xff},
{0x044f,0xb4},
{0x0460,0xff},
{0x0461,0x69},
{0x0462,0xff},
{0x0463,0xbb},
{0x0464,0x00},
{0x0465,0x84},
{0x0466,0x00},
{0x0467,0xa3},
{0x0468,0x00},
{0x0469,0x0e},
{0x046a,0x00},
{0x046b,0x76},
{0x046c,0xff},
{0x046d,0xaf},
{0x046e,0xff},
{0x046f,0xf5},
{0x0470,0xff},
{0x0471,0x8a},
{0x0472,0xff},
{0x0473,0x5a},
{0x0474,0x00},
{0x0475,0xef},
{0x0476,0x01},
{0x0477,0x16},
{0x0478,0xff},
{0x0479,0xd4},
{0x047a,0x00},
{0x047b,0x02},
{0x047c,0x00},
{0x047d,0x2c},
{0x047e,0xff},
{0x047f,0x95},
{0x0490,0xff},
{0x0491,0x9b},
{0x0492,0xff},
{0x0493,0x91},
{0x0494,0x00},
{0x0495,0x6f},
{0x0496,0x00},
{0x0497,0x95},
{0x0498,0xff},
{0x0499,0xd5},
{0x049a,0x01},
{0x049b,0x20},
{0x049c,0xff},
{0x049d,0xfb},
{0x049e,0xff},
{0x049f,0xe1},
{0x0480,0xff},
{0x0481,0x5a},
{0x0482,0xff},
{0x0483,0x91},
{0x0484,0x00},
{0x0485,0x8c},
{0x0486,0x00},
{0x0487,0x9f},
{0x0488,0x00},
{0x0489,0x29},
{0x048a,0x00},
{0x048b,0x53},
{0x048c,0xff},
{0x048d,0x80},
{0x048e,0xff},
{0x048f,0xf7},
{0x04a0,0xff},
{0x04a1,0x6c},
{0x04a2,0xff},
{0x04a3,0xb9},
{0x04a4,0x00},
{0x04a5,0x81},
{0x04a6,0x00},
{0x04a7,0x93},
{0x04a8,0x00},
{0x04a9,0x1c},
{0x04aa,0x00},
{0x04ab,0x39},
{0x04ac,0xff},
{0x04ad,0x9f},
{0x04ae,0x00},
{0x04af,0x0e},
{0x04b0,0xff},
{0x04b1,0xe0},
{0x04b2,0xff},
{0x04b3,0x7b},
{0x04b4,0x00},
{0x04b5,0xaa},
{0x04b6,0x00},
{0x04b7,0xc8},
{0x04b8,0xff},
{0x04b9,0xe1},
{0x04ba,0x00},
{0x04bb,0x0e},
{0x04bc,0x00},
{0x04bd,0x0b},
{0x04be,0xff},
{0x04bf,0xff},
{0x04d0,0xff},
{0x04d1,0xac},
{0x04d2,0xff},
{0x04d3,0x93},
{0x04d4,0x00},
{0x04d5,0x64},
{0x04d6,0x00},
{0x04d7,0x83},
{0x04d8,0xff},
{0x04d9,0xdb},
{0x04da,0x00},
{0x04db,0xa8},
{0x04dc,0xff},
{0x04dd,0xf5},
{0x04de,0x00},
{0x04df,0x15},
{0x04c0,0xff},
{0x04c1,0x5d},
{0x04c2,0xff},
{0x04c3,0x9c},
{0x04c4,0x00},
{0x04c5,0x82},
{0x04c6,0x00},
{0x04c7,0x96},
{0x04c8,0x00},
{0x04c9,0x33},
{0x04ca,0x00},
{0x04cb,0x07},
{0x04cc,0xff},
{0x04cd,0x71},
{0x04ce,0x00},
{0x04cf,0x11},
{0x04e0,0xff},
{0x04e1,0x6d},
{0x04e2,0xff},
{0x04e3,0xb8},
{0x04e4,0x00},
{0x04e5,0x84},
{0x04e6,0x00},
{0x04e7,0x96},
{0x04e8,0xff},
{0x04e9,0xc0},
{0x04ea,0x00},
{0x04eb,0x6d},
{0x04ec,0xff},
{0x04ed,0xbb},
{0x04ee,0x00},
{0x04ef,0x00},
{0x04f0,0xff},
{0x04f1,0xe0},
{0x04f2,0xff},
{0x04f3,0x95},
{0x04f4,0x00},
{0x04f5,0xa7},
{0x04f6,0x00},
{0x04f7,0xc8},
{0x04f8,0xff},
{0x04f9,0xde},
{0x04fa,0x00},
{0x04fb,0x7e},
{0x04fc,0x00},
{0x04fd,0x36},
{0x04fe,0x00},
{0x04ff,0x10},
{0x0510,0xff},
{0x0511,0xc1},
{0x0512,0xff},
{0x0513,0x9f},
{0x0514,0x00},
{0x0515,0x6a},
{0x0516,0x00},
{0x0517,0x89},
{0x0518,0xff},
{0x0519,0xdc},
{0x051a,0x00},
{0x051b,0x55},
{0x051c,0x00},
{0x051d,0x09},
{0x051e,0x00},
{0x051f,0x0d},
{0x0500,0xff},
{0x0501,0x60},
{0x0502,0xff},
{0x0503,0x9e},
{0x0504,0x00},
{0x0505,0x81},
{0x0506,0x00},
{0x0507,0x9c},
{0x0508,0xff},
{0x0509,0xc0},
{0x050a,0x00},
{0x050b,0x40},
{0x050c,0xff},
{0x050d,0x8e},
{0x050e,0x00},
{0x050f,0x00},
{0x0561,0x0e},
{0x0562,0x01},
{0x0563,0x01},
{0x0564,0x06},
#endif // LSC end 
#if 1  //new LSC start - 0306
{0x0420,0x00},
{0x0421,0x26},
{0x0422,0xff},
{0x0423,0x03},
{0x0424,0x00},
{0x0425,0x63},
{0x0426,0x00},
{0x0427,0x8e},
{0x0428,0xff},
{0x0429,0xf3},
{0x042A,0x00},
{0x042B,0x26},
{0x042C,0xff},
{0x042D,0xc8},
{0x042E,0xff},
{0x042F,0x5f},
{0x0430,0x00},
{0x0431,0x10}, 
{0x0432,0xfe},
{0x0433,0xcf}, 
{0x0434,0x00},
{0x0435,0xc2}, 
{0x0436,0x00},
{0x0437,0xf2}, 
{0x0438,0xff},
{0x0439,0xd0}, 
{0x043A,0x00},
{0x043B,0xa1}, 
{0x043C,0x00},
{0x043D,0x2f}, 
{0x043E,0xfe},
{0x043F,0xdc},
{0x0450,0x00},
{0x0451,0x36},  
{0x0452,0xff},
{0x0453,0x60},  
{0x0454,0x00},
{0x0455,0x43},  
{0x0456,0x00},
{0x0457,0x6b},  
{0x0458,0xff},
{0x0459,0xdf},  
{0x045A,0x00},
{0x045B,0x83},  
{0x045C,0xff},
{0x045D,0xeb},  
{0x045E,0xff},
{0x045F,0x5f},
{0x0440,0x00},
{0x0441,0x36},        
{0x0442,0xff},
{0x0443,0x3e},        
{0x0444,0x00},
{0x0445,0x6d},        
{0x0446,0x00},
{0x0447,0x87},        
{0x0448,0x00},
{0x0449,0x0f},        
{0x044A,0xff},
{0x044B,0xd3},        
{0x044C,0x00},
{0x044D,0x18},        
{0x044E,0xff},
{0x044F,0x4f},
{0x0561,0x08},
{0x0460,0x00},
{0x0461,0x22},
{0x0462,0xff},
{0x0463,0x0a},
{0x0464,0x00},
{0x0465,0x63},
{0x0466,0x00},
{0x0467,0x8b},
{0x0468,0xff},
{0x0469,0xf9},
{0x046A,0x00},
{0x046B,0x17},
{0x046C,0xff},
{0x046D,0xbd},
{0x046E,0xff},
{0x046F,0x65},
{0x0470,0x00},
{0x0471,0x15}, 
{0x0472,0xfe},
{0x0473,0xd7}, 
{0x0474,0x00},
{0x0475,0xb6}, 
{0x0476,0x00},
{0x0477,0xe5}, 
{0x0478,0xff},
{0x0479,0xd0}, 
{0x047A,0x00},
{0x047B,0xb2}, 
{0x047C,0x00},
{0x047D,0x2c}, 
{0x047E,0xfe},
{0x047F,0xe6},
{0x0490,0x00},
{0x0491,0x2b},  
{0x0492,0xff},
{0x0493,0x71},  
{0x0494,0x00},
{0x0495,0x43},  
{0x0496,0x00},
{0x0497,0x69},  
{0x0498,0xff},
{0x0499,0xd7},  
{0x049A,0x00},
{0x049B,0x59},  
{0x049C,0xff},
{0x049D,0xde},  
{0x049E,0xff},
{0x049F,0x76},                                    
{0x0480,0x00},
{0x0481,0x38},        
{0x0482,0xff},
{0x0483,0x3a},        
{0x0484,0x00},
{0x0485,0x6b},        
{0x0486,0x00},
{0x0487,0x86},        
{0x0488,0x00},
{0x0489,0x14},        
{0x048A,0xff},
{0x048B,0xd7},        
{0x048C,0x00},
{0x048D,0x13},        
{0x048E,0xff},
{0x048F,0x58},
{0x0562,0x08},
{0x04A0,0x00},
{0x04A1,0x17},
{0x04A2,0xff},
{0x04A3,0x2e},
{0x04A4,0x00},
{0x04A5,0x5d},
{0x04A6,0x00},
{0x04A7,0x7a},
{0x04A8,0xff},
{0x04A9,0xf9},
{0x04AA,0xff},
{0x04AB,0xc0},
{0x04AC,0xff},
{0x04AD,0xc7},
{0x04AE,0xff},
{0x04AF,0x7d},
{0x04B0,0x00},
{0x04B1,0x13}, 
{0x04B2,0xff},
{0x04B3,0x45}, 
{0x04B4,0x00},
{0x04B5,0x70}, 
{0x04B6,0x00},
{0x04B7,0x96}, 
{0x04B8,0xff},
{0x04B9,0xde}, 
{0x04BA,0x00},
{0x04BB,0x17}, 
{0x04BC,0x00},
{0x04BD,0x23}, 
{0x04BE,0xff},
{0x04BF,0x46},
{0x04D0,0x00},
{0x04D1,0x17},  
{0x04D2,0xff},
{0x04D3,0x99},  
{0x04D4,0x00},
{0x04D5,0x3d},  
{0x04D6,0x00},
{0x04D7,0x5f},  
{0x04D8,0xff},
{0x04D9,0xd1},  
{0x04DA,0xff},
{0x04DB,0xe2},  
{0x04DC,0xff},
{0x04DD,0xc6},  
{0x04DE,0xff},
{0x04DF,0x9c},                                    
{0x04C0,0x00},
{0x04C1,0x32},        
{0x04C2,0xff},
{0x04C3,0x4e},        
{0x04C4,0x00},
{0x04C5,0x5e},        
{0x04C6,0x00},
{0x04C7,0x7c},        
{0x04C8,0x00},
{0x04C9,0x17},        
{0x04CA,0xff},
{0x04CB,0x9b},        
{0x04CC,0x00},
{0x04CD,0x06},        
{0x04CE,0xff},
{0x04CF,0x70},
{0x0563,0x05},
{0x04E0,0x00},
{0x04E1,0x11},
{0x04E2,0xff},
{0x04E3,0x20},
{0x04E4,0x00},
{0x04E5,0x5f},
{0x04E6,0x00},
{0x04E7,0x7b},
{0x04E8,0x00},
{0x04E9,0x02},
{0x04EA,0xff},
{0x04EB,0xf1},
{0x04EC,0xff},
{0x04ED,0xb7},
{0x04EE,0xff},
{0x04EF,0x7b},
{0x04F0,0x00},
{0x04F1,0x1c}, 
{0x04F2,0xff},
{0x04F3,0x08}, 
{0x04F4,0x00},
{0x04F5,0x7e}, 
{0x04F6,0x00},
{0x04F7,0xac}, 
{0x04F8,0xff},
{0x04F9,0xeb}, 
{0x04FA,0x00},
{0x04FB,0x93}, 
{0x04FC,0x00},
{0x04FD,0x1b}, 
{0x04FE,0xff},
{0x04FF,0x11}, 
{0x0510,0xff},
{0x0511,0xfe},  
{0x0512,0xff},
{0x0513,0x8e},  
{0x0514,0x00},
{0x0515,0x4a},  
{0x0516,0x00},
{0x0517,0x6e},  
{0x0518,0xff},
{0x0519,0xbb},  
{0x051A,0xff},
{0x051B,0xcc},  
{0x051C,0xff},
{0x051D,0x9a},  
{0x051E,0xff},
{0x051F,0xa2},  
{0x0500,0x00},
{0x0501,0x30},        
{0x0502,0xff},
{0x0503,0x36},        
{0x0504,0x00},
{0x0505,0x5d},        
{0x0506,0x00},
{0x0507,0x81},        
{0x0508,0x00},
{0x0509,0x1f},        
{0x050A,0xff},
{0x050B,0xd4},        
{0x050C,0xff},
{0x050D,0xfe},        
{0x050E,0xff},
{0x050F,0x75},
{0x0564,0x00},
#endif //new LSC end
{0x0324,0x39},
{0x0325,0xAE},
{0x0326,0x3A},
{0x0327,0x29},
{0x0328,0x3B},
{0x0329,0x0A},
{0x032A,0x3B},
{0x032B,0x62},
{0x0320,0x01},
{0x0321,0x04},
{0x0322,0x01},
{0x0323,0x01},
{0x0330,0x01},
{0x0384,0x00},
{0x0337,0x01},
{0x03EC,0x39},
{0x03ED,0x85},
{0x03FC,0x3A},
{0x03FD,0x14},
{0x040C,0x3A},
{0x040D,0xF6},
{0x041C,0x3B},
{0x041D,0x9A},
{0x03E0,0xB6},
{0x03E1,0x04},
{0x03E2,0xBB},
{0x03E3,0xE9},
{0x03E4,0xBC},
{0x03E5,0x70},
{0x03E6,0x37},
{0x03E7,0x02},
{0x03E8,0xBC},
{0x03E9,0x00},
{0x03EA,0xBF},
{0x03EB,0x12},
{0x03F0,0xBA},
{0x03F1,0x7B},
{0x03F2,0xBA},
{0x03F3,0x83},
{0x03F4,0xBB},
{0x03F5,0xBC},
{0x03F6,0x38},
{0x03F7,0x2D},
{0x03F8,0xBB},
{0x03F9,0x23},
{0x03FA,0xBD},
{0x03FB,0xAC},
{0x0400,0xBE},
{0x0401,0x96},
{0x0402,0xB9},
{0x0403,0xBE},
{0x0404,0xBB},
{0x0405,0x57},
{0x0406,0x3A},
{0x0407,0xBB},
{0x0408,0xB3},
{0x0409,0x17},
{0x040A,0xBE},
{0x040B,0x66},
{0x0410,0xBB},
{0x0411,0x2A},
{0x0412,0xBA},
{0x0413,0x00},
{0x0414,0xBB},
{0x0415,0x10},
{0x0416,0xB8},
{0x0417,0xCD},
{0x0418,0xB7},
{0x0419,0x5C},
{0x041A,0xBB},
{0x041B,0x6C},
{0x01f8,0x3c},
{0x01f9,0x00},
{0x01fa,0x00},
{0x02a2,0x3e},
{0x02a3,0x00},
{0x02a4,0x3e},
{0x02a5,0x00},
{0x02a6,0x3e},
{0x02a7,0x00},
{0x02a8,0x3e},
{0x02a9,0x00},
{0x056c,0x42},
{0x056d,0x00},
{0x056e,0x42},
{0x056f,0x00},
{0x0570,0x42},
{0x0571,0x00},
{0x0572,0x42},
{0x0573,0x00},
{0x0081,0x55},  //6E->55
{0x0588,0x00},
{0x0589,0x5A},
{0x058A,0xEE},
{0x058B,0x69},
{0x058C,0x49},
{0x058D,0x3D},
{0x058E,0x3D},
{0x0080,0x6C},
{0x0082,0x5A},
{0x0010,0x01},  

	{SEQUENCE_END, 0x00}
};

static struct reginfo sensor_init_data2[] =
{
{0x4708,0x00},
{0x4709,0x00},
{0x4710,0x00},
{0x4711,0x00},
//AF intial setting
#if 0
{0x065A,0x00},
{0x06C9,0x01},
{0x06CD,0x01},
{0x06CE,0xBD},
{0x06CF,0x00},
{0x06D0,0x93},
{0x06D1,0x02},
{0x06D2,0x30},
{0x06D3,0xD4},
{0x06D4,0x01},
{0x06DB,0x59},
{0x06DC,0x0D},
{0x0730,0x00},
{0x0731,0x00},
{0x0732,0x03},
{0x0733,0xFF},
{0x0734,0x03},
{0x0735,0xff},
{0x0755,0x01},
{0x0756,0x00},//03->00
{0x075B,0x01},//00
{0x075E,0x00},
{0x0764,0x01},
{0x0766,0x01},//00
{0x0768,0x01},
{0x076A,0x00},//01
{0x0758,0x01},
{0x075C,0x01},
{0x0770,0x98},
{0x0771,0x19},
{0x0772,0x1B},
{0x0774,0x01},
{0x0775,0x4A},
{0x0777,0x00},
{0x0778,0x45},
{0x0779,0x00},
{0x077A,0x02},
{0x077D,0x01},
{0x077E,0x03},
{0x0783,0x10},
{0x0785,0x14},
{0x0846,0x06},
{0x0847,0x05},
#endif // old af @ vcm end -03-06
#if 1 //03-06 new af @ vcm
{0x0659,0x01},
{0x065A,0x00},
{0x06C9,0x01},
{0x06CD,0x01},
{0x06CE,0xBD},
{0x06CF,0x00},
{0x06D0,0x93},
{0x06D1,0x02},
{0x06D2,0x30},
{0x06D3,0xD4},
{0x06D4,0x01},
{0x06DB,0x59},
{0x06DC,0x0d},
{0x0730,0x00},
{0x0731,0x00},
{0x0732,0x03},
{0x0733,0xFF},
{0x0734,0x03},
{0x0735,0x70},
{0x0755,0x01},
{0x0756,0x00},
{0x075A,0x00},
{0x075B,0x01},
{0x075E,0x00},
{0x0764,0x01},
{0x0766,0x01},
{0x0768,0x01},
{0x076A,0x00},
{0x0758,0x01},
{0x075C,0x01},
{0x0770,0x98},
{0x0771,0x19},
{0x0772,0x1B},
{0x0774,0x01},
{0x0775,0x4a},
{0x0777,0x00},
{0x0778,0x45},
{0x0779,0x00},
{0x077A,0x02},
{0x077D,0x01},
{0x077E,0x03},
{0x0783,0x10},
{0x0785,0x14},
{0x0788,0x04},
{0x0846,0x06},
{0x0847,0x05},
#endif
{0xC41A,0x05},
{0xC423,0x11},
{0xC427,0x11},
{0x300B,0x09},

{0x7000,0x08}, 
{0x5200,0x09}, 
{0x00B2,0x50}, 
{0x00B3,0x80}, 
{0x00B5,0x01}, 
{0x0030,0x14}, 
{0x0040,0x01}, //AB2
{0x0041,0x03}, //SVGA mode

{0x0010,0x01},

	{SEQUENCE_END, 0x00}
};  //sensor_init_data2 end



/* 720p 15fps @ 1280x720 */

static struct reginfo sensor_720p[]=
{
	
	{SEQUENCE_END, 0x00}
};

/* 	1080p, 0x15fps, 0xyuv @1920x1080 */

static struct reginfo sensor_1080p[]=
{

	{SEQUENCE_END, 0x00}
};

/* 2592X1944 QSXGA */
static struct reginfo sensor_qsxga[] =
{
	
//{0x0010,0x02},

{0x7000,0x08},
{0x5200,0x09},
//{0x00B2,0x50},
//{0x00B3,0x80},
//{0x00B5,0x02},
//{0x0030,0x11},
{0x0040,0x00},
{0x0041,0x00},
{0x0010,0x01},

	{SEQUENCE_END, 0x00}
};
/* 2048*1536 QXGA */
static struct reginfo sensor_qxga[] =
{
	{SEQUENCE_END, 0x00}
};

/* 1600X1200 UXGA */
static struct reginfo sensor_uxga[] =
{
#if 0
{0x7000,0x08},
{0x5200,0x09},
//{0x00B2,0x50},
//{0x00B3,0x80},
//{0x00B5,0x02},
//{0x0030,0x14},
{0x0040,0x00},
{0x0041,0x01},
{0x0010,0x01},
#endif
	{SEQUENCE_END, 0x00}
};

/* 1280X1024 SXGA */
static struct reginfo sensor_sxga[] =
{
#if 0
{0x7000,0x08},
{0x5200,0x09},
//{0x00B2,0x50},
//{0x00B3,0x80},
//{0x00B5,0x02},
//{0x0030,0x14},
{0x0040,0x00},
{0x0041,0x02},
{0x0010,0x01},
#endif
	{SEQUENCE_END, 0x00}
};
/* 1024X768 XGA */
static struct reginfo sensor_xga[] =
{
#if 0
{0x7000,0x08},
{0x5200,0x09},
//{0x00B2,0x50},
//{0x00B3,0x80},
//{0x00B5,0x02},
//{0x0030,0x11},
{0x0040,0x01},
{0x0041,0x0a},
{0x0042,0x05},
{0x0043,0x00},
{0x0044,0x03},
{0x0045,0x20},
{0x0010,0x01},
#endif
	{SEQUENCE_END, 0x00}
};
/* 800X600 SVGA*/
static struct reginfo sensor_svga[] =
{
//{0x0010,0x02},

//{0x7000,0x08},
//{0x5200,0x09},
//{0x00B2,0x50},
//{0x00B3,0x80},
//{0x00B5,0x02},
//{0x0030,0x14},
{0x0040,0x01},
{0x0041,0x03},

{0x00ed,0x0A},
{0x00ee,0x1E},

//{0x0010,0x01},
	
	{SEQUENCE_END, 0x00}
};

/* 640X480 VGA */
static struct reginfo sensor_vga[] =
{
	

{0x7000,0x08},
{0x5200,0x09},
{0x0040,0x01},
{0x0041,0x04},
{0x00ed,0x1E},
{0x00ee,0x1E},
{0x331e,0x00},

	{SEQUENCE_END, 0x00}
};
/* 352X288 CIF */
static struct reginfo sensor_cif[] =
{

	{SEQUENCE_END, 0x00}
};

/* 320*240 QVGA */
static  struct reginfo sensor_qvga[] =
{

	{SEQUENCE_END, 0x00}
};

/* 176X144 QCIF*/
static struct reginfo sensor_qcif[] =
{

	{SEQUENCE_END, 0x00}
};
#endif
static  struct reginfo sensor_ClrFmt_YUYV[]=
{

	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_ClrFmt_UYVY[]=
{

	{SEQUENCE_END, 0x00}
};


#if CONFIG_SENSOR_WhiteBalance
static  struct reginfo sensor_WhiteB_Auto[]=
{
	{0x01A0,0x01},
	{SEQUENCE_END, 0x00}
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static  struct reginfo sensor_WhiteB_Cloudy[]=
{
	{0x01A0,0x04},
	{SEQUENCE_END, 0x00}
};
/* ClearDay Colour Temperature : 5000K - 6500K  */
static  struct reginfo sensor_WhiteB_ClearDay[]=
{
	{0x01A0,0x05},
	{SEQUENCE_END, 0x00}
};
/* Office Colour Temperature : 3500K - 5000K  */
static  struct reginfo sensor_WhiteB_TungstenLamp1[]=
{
	{0x01A0,0x07},
	{SEQUENCE_END, 0x00}
};
/* Home Colour Temperature : 2500K - 3500K  */
static  struct reginfo sensor_WhiteB_TungstenLamp2[]=
{
	{0x01A0,0x08},
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
    sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};
#endif

#if CONFIG_SENSOR_Brightness
static  struct reginfo sensor_Brightness0[]=
{
	{0x082,0x34},
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Brightness1[]=
{
	{0x082,0x44},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness2[]=
{
	{0x082,0x54},
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Brightness3[]=
{
	{0x082,0x64},
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Brightness4[]=
{
	{0x082,0x74},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness5[]=
{
	{0x082,0x84},
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
    sensor_Brightness4, sensor_Brightness5,NULL,
};

#endif

#if CONFIG_SENSOR_Effect
static  struct reginfo sensor_Effect_Normal[] =
{
	{0x0380,0x0},
	{0x0381,0x0},
	{0x0382,0x0},
	{0x0384,0x0},
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Effect_WandB[] =
{
	{0x0380,0x0},
	{0x0381,0x0},
	{0x0382,0x0},
	{0x0384,0x5},
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Effect_Sepia[] =
{
	{0x0380,0x0},
	{0x0381,0x0},
	{0x0382,0x0},
	{0x0384,0x6},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Negative[] =
{
    //Negative
	{0x0380,0x0},
	{0x0381,0x0},
	{0x0382,0x0},
	{0x0384,0x8},
	{SEQUENCE_END, 0x00}
};static  struct reginfo sensor_Effect_Bluish[] =
{
    // Bluish
{0x0380,0x0},
	{0x0381,0x0},
	{0x0382,0x0},
	{0x0384,0x4},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Green[] =
{
    //  Greenish
	{0x0380,0x0},
	{0x0381,0x0},
	{0x0382,0x0},
	{0x0384,0x3},
	{SEQUENCE_END, 0x00}
};
/*static  struct reginfo sensor_Effect_Reddish[] =
{
    //  Greenish

	{SEQUENCE_END, 0x00}
};*/

static struct reginfo *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
    sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};
#endif
#if CONFIG_SENSOR_Exposure
static  struct reginfo sensor_Exposure0[]=
{
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Exposure1[]=
{
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Exposure2[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Exposure3[]=
{
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Exposure4[]=
{
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Exposure5[]=
{
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Exposure6[]=
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
    sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
};
#endif
#if CONFIG_SENSOR_Saturation
static  struct reginfo sensor_Saturation0[]=
{
	{0x081,0x34},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Saturation1[]=
{
	{0x081,0x44},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Saturation2[]=
{
	{0x081,0x54},
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Saturation3[]=
{
	{0x081,0x64},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Saturation4[]=
{
	{0x081,0x74},
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Saturation5[]=
{
	{0x081,0x84},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Saturation6[]=
{
	{0x081,0x94},
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2,sensor_Saturation3, sensor_Saturation4, sensor_Saturation5,sensor_Saturation6, NULL,};

#endif
#if CONFIG_SENSOR_Contrast
static  struct reginfo sensor_Contrast0[]=
{
	{0x080,0x34},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast1[]=
{
	{0x080,0x44},
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Contrast2[]=
{
	{0x080,0x54},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast3[]=
{
	{0x080,0x64},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast4[]=
{
	{0x080,0x74},
	{SEQUENCE_END, 0x00}
};


static  struct reginfo sensor_Contrast5[]=
{
	{0x080,0x84},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast6[]=
{
	{0x080,0x94},
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
    sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};

#endif
#if CONFIG_SENSOR_Mirror
static  struct reginfo sensor_MirrorOn[]=
{
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_MirrorOff[]=
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_MirrorSeqe[] = {sensor_MirrorOff, sensor_MirrorOn,NULL,};
#endif
#if CONFIG_SENSOR_Flip
static  struct reginfo sensor_FlipOn[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_FlipOff[]=
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_FlipSeqe[] = {sensor_FlipOff, sensor_FlipOn,NULL,};

#endif
#if CONFIG_SENSOR_Scene
static  struct reginfo sensor_SceneAuto[] =
{

	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_SceneNight[] =
{
    //15fps ~ 3.75fps night mode for 60/50Hz light environment, 24Mhz clock input,24Mzh pclk
	
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_SceneSeqe[] = {sensor_SceneAuto, sensor_SceneNight,NULL,};

#endif
#if CONFIG_SENSOR_DigitalZoom
static struct reginfo sensor_Zoom0[] =
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo sensor_Zoom1[] =
{
	{SEQUENCE_END, 0x00}
};

static struct reginfo sensor_Zoom2[] =
{
	{SEQUENCE_END, 0x00}
};


static struct reginfo sensor_Zoom3[] =
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_ZoomSeqe[] = {sensor_Zoom0, sensor_Zoom1, sensor_Zoom2, sensor_Zoom3, NULL};
#endif
static const struct v4l2_querymenu sensor_menus[] =
{
	#if CONFIG_SENSOR_WhiteBalance
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 0,  .name = "auto",  .reserved = 0, }, {  .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 1, .name = "incandescent",  .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 2,  .name = "fluorescent", .reserved = 0,}, {  .id = V4L2_CID_DO_WHITE_BALANCE, .index = 3,  .name = "daylight", .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 4,  .name = "cloudy-daylight", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Effect
    { .id = V4L2_CID_EFFECT,  .index = 0,  .name = "none",  .reserved = 0, }, {  .id = V4L2_CID_EFFECT,  .index = 1, .name = "mono",  .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 2,  .name = "negative", .reserved = 0,}, {  .id = V4L2_CID_EFFECT, .index = 3,  .name = "sepia", .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 4, .name = "posterize", .reserved = 0,} ,{ .id = V4L2_CID_EFFECT,  .index = 5,  .name = "aqua", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Scene
    { .id = V4L2_CID_SCENE,  .index = 0, .name = "auto", .reserved = 0,} ,{ .id = V4L2_CID_SCENE,  .index = 1,  .name = "night", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Flash
    { .id = V4L2_CID_FLASH,  .index = 0,  .name = "off",  .reserved = 0, }, {  .id = V4L2_CID_FLASH,  .index = 1, .name = "auto",  .reserved = 0,},
    { .id = V4L2_CID_FLASH,  .index = 2,  .name = "on", .reserved = 0,}, {  .id = V4L2_CID_FLASH, .index = 3,  .name = "torch", .reserved = 0,},
    #endif
};

static  struct v4l2_queryctrl sensor_controls[] =
{
	#if CONFIG_SENSOR_WhiteBalance
    {
        .id		= V4L2_CID_DO_WHITE_BALANCE,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "White Balance Control",
        .minimum	= 0,
        .maximum	= 4,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Brightness
	{
        .id		= V4L2_CID_BRIGHTNESS,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Brightness Control",
        .minimum	= -3,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Effect
	{
        .id		= V4L2_CID_EFFECT,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Effect Control",
        .minimum	= 0,
        .maximum	= 5,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Exposure
	{
        .id		= V4L2_CID_EXPOSURE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Exposure Control",
        .minimum	= 0,
        .maximum	= 6,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Saturation
	{
        .id		= V4L2_CID_SATURATION,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Saturation Control",
        .minimum	= 0,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Contrast
	{
        .id		= V4L2_CID_CONTRAST,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Contrast Control",
        .minimum	= -3,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Mirror
	{
        .id		= V4L2_CID_HFLIP,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Mirror Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    },
    #endif

	#if CONFIG_SENSOR_Flip
	{
        .id		= V4L2_CID_VFLIP,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Flip Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    },
    #endif

	#if CONFIG_SENSOR_Scene
    {
        .id		= V4L2_CID_SCENE,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Scene Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_DigitalZoom
    {
        .id		= V4L2_CID_ZOOM_RELATIVE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= -1,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_ZOOM_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= 0,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Focus
	/*{
        .id		= V4L2_CID_FOCUS_RELATIVE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Focus Control",
        .minimum	= -1,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_FOCUS_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 255,
        .step		= 1,
        .default_value = 125,
    },*/
	{
        .id		= V4L2_CID_FOCUSZONE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "FocusZone Control",
        .minimum	= -1,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },{
        .id		= V4L2_CID_FOCUS_AUTO,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },{
        .id		= V4L2_CID_FOCUS_CONTINUOUS,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Flash
	{
        .id		= V4L2_CID_FLASH,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Flash Control",
        .minimum	= 0,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
	#endif
};

static int sensor_probe(struct i2c_client *client, const struct i2c_device_id *did);
static int sensor_video_probe(struct soc_camera_device *icd, struct i2c_client *client);
static int sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int sensor_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int sensor_g_ext_controls(struct v4l2_subdev *sd,  struct v4l2_ext_controls *ext_ctrl);
static int sensor_s_ext_controls(struct v4l2_subdev *sd,  struct v4l2_ext_controls *ext_ctrl);
static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg);
static int sensor_resume(struct soc_camera_device *icd);
static int sensor_set_bus_param(struct soc_camera_device *icd,unsigned long flags);
static unsigned long sensor_query_bus_param(struct soc_camera_device *icd);
static int sensor_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
#if CONFIG_SENSOR_WhiteBalance
static int sensor_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
#endif
static int sensor_deactivate(struct i2c_client *client);
static bool sensor_fmt_capturechk(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf);
static bool sensor_fmt_videochk(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf);

static struct soc_camera_ops sensor_ops =
{
    .suspend                     = sensor_suspend,
    .resume                       = sensor_resume,
    .set_bus_param		= sensor_set_bus_param,
    .query_bus_param	= sensor_query_bus_param,
    .controls		= sensor_controls,
    .menus                         = sensor_menus,
    .num_controls		= ARRAY_SIZE(sensor_controls),
    .num_menus		= ARRAY_SIZE(sensor_menus),
};
/* only one fixed colorspace per pixelcode */
struct sensor_datafmt {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

/* Find a data format by a pixel code in an array */
static const struct sensor_datafmt *sensor_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct sensor_datafmt *fmt,
	int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (fmt[i].code == code)
			return fmt + i;

	return NULL;
}

static const struct sensor_datafmt sensor_colour_fmts[] = {
    {V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
    {V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG}	
};
enum sensor_wq_cmd
{
    WqCmd_af_init,
    WqCmd_af_single,
    WqCmd_af_special_pos,
    WqCmd_af_far_pos,
    WqCmd_af_near_pos,
    WqCmd_af_continues,
    WqCmd_af_continues_pause,
    WqCmd_af_return_idle,
};
enum sensor_wq_result
{
    WqRet_success = 0,
    WqRet_fail = -1,
    WqRet_inval = -2
};
struct sensor_work
{
	struct i2c_client *client;
	struct delayed_work dwork;
	enum sensor_wq_cmd cmd;
    wait_queue_head_t done;
    enum sensor_wq_result result;
    bool wait;
    int var;    
    int zone_pos[4];
};

typedef struct sensor_info_priv_s
{
    int whiteBalance;
    int brightness;
    int contrast;
    int saturation;
    int effect;
    int scene;
    int digitalzoom;
    int focus;
	int auto_focus;
	int affm_reinit;
    int flash;
    int exposure;
	bool snap2preview;
	bool video2preview;
    unsigned char mirror;                                        /* HFLIP */
    unsigned char flip;                                          /* VFLIP */
    struct reginfo *winseqe_cur_addr;
	struct sensor_datafmt fmt;
	unsigned int enable;
	unsigned int funmodule_state;
} sensor_info_priv_t;



struct sensor_parameter
{
    char af_pos[2];
};

struct sensor
{
    struct v4l2_subdev subdev;
    struct i2c_client *client;
    sensor_info_priv_t info_priv;
	struct sensor_parameter parameter;
	struct workqueue_struct *sensor_wq;
	struct mutex wq_lock;
    int model;	/* V4L2_IDENT_OV* codes from v4l2-chip-ident.h */
#if CONFIG_SENSOR_I2C_NOSCHED
	atomic_t tasklock_cnt;
#endif
	struct rk29camera_platform_data *sensor_io_request;
    struct rk29camera_gpio_res *sensor_gpio_res;
};

static struct sensor* to_sensor(const struct i2c_client *client)
{
    return container_of(i2c_get_clientdata(client), struct sensor, subdev);
}

static int sensor_task_lock(struct i2c_client *client, int lock)
{
#if CONFIG_SENSOR_I2C_NOSCHED
	int cnt = 3;
    struct sensor *sensor = to_sensor(client);

	if (lock) {
		if (atomic_read(&sensor->tasklock_cnt) == 0) {
			while ((atomic_read(&client->adapter->bus_lock.count) < 1) && (cnt>0)) {
				SENSOR_TR("\n %s will obtain i2c in atomic, but i2c bus is locked! Wait...\n",SENSOR_NAME_STRING());
				msleep(35);
				cnt--;
			}
			if ((atomic_read(&client->adapter->bus_lock.count) < 1) && (cnt<=0)) {
				SENSOR_TR("\n %s obtain i2c fail in atomic!!\n",SENSOR_NAME_STRING());
				goto sensor_task_lock_err;
			}
			preempt_disable();
		}

		atomic_add(1, &sensor->tasklock_cnt);
	} else {
		if (atomic_read(&sensor->tasklock_cnt) > 0) {
			atomic_sub(1, &sensor->tasklock_cnt);

			if (atomic_read(&sensor->tasklock_cnt) == 0)
				preempt_enable();
		}
	}
	return 0;
sensor_task_lock_err:
	return -1;   
#else
    return 0;
#endif

}


#if CONFIG_SENSOR_WRITE_REGS
static int sensor_write_regs(struct i2c_client *client,  u8 *reg_info, int num)
{
	int err=0,cnt;
	struct i2c_msg msg[1];

	msg->len = num;	
	msg->addr = client->addr;	
	msg->flags = client->flags;	
	msg->buf = reg_info;	
	msg->scl_rate = CONFIG_SENSOR_I2C_SPEED;         /* ddl@rock-chips.com : 100kHz */	
	msg->read_type = 0;               /* fpga i2c:0==I2C_NORMAL : direct use number not enum for don't want include spi_fpga.h */	

	
	cnt= 3;	
	err = -EAGAIN;
	
	while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */		
		err = i2c_transfer(client->adapter, msg, 1);		
		if (err >= 0) {		            
			return 0;		
		} else {		            
			SENSOR_TR("\n %s write reg failed, try to write again!\n",	SENSOR_NAME_STRING());		            
			udelay(10);	
		}	
	}
	
	return err;

}

#endif




/* sensor register write */
static int sensor_write(struct i2c_client *client, u16 reg, u8 val)
{
    int err,cnt;
    u8 buf[3];
    struct i2c_msg msg[1];

    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;
    buf[2] = val;

    msg->addr = client->addr;
    msg->flags = client->flags;
    msg->buf = buf;
    msg->len = sizeof(buf);
    msg->scl_rate = CONFIG_SENSOR_I2C_SPEED;         /* ddl@rock-chips.com : 100kHz */
    msg->read_type = 0;               /* fpga i2c:0==I2C_NORMAL : direct use number not enum for don't want include spi_fpga.h */

    cnt = 3;
    err = -EAGAIN;

    while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 1);

        if (err >= 0) {
            return 0;
        } else {
            SENSOR_TR("\n %s write reg(0x%x, val:0x%x) failed, try to write again!\n",SENSOR_NAME_STRING(),reg, val);
            udelay(10);
        }
    }

    return err;
}

/* sensor register read */
static int sensor_read(struct i2c_client *client, u16 reg, u8 *val)
{
    int err,cnt;
    u8 buf[2];
    struct i2c_msg msg[2];

    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;

    msg[0].addr = client->addr;
    msg[0].flags = client->flags;
    msg[0].buf = buf;
    msg[0].len = sizeof(buf);
    msg[0].scl_rate = CONFIG_SENSOR_I2C_SPEED;       /* ddl@rock-chips.com : 100kHz */
    msg[0].read_type = 2;   /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

    msg[1].addr = client->addr;
    msg[1].flags = client->flags|I2C_M_RD;
    msg[1].buf = buf;
    msg[1].len = 1;
    msg[1].scl_rate = CONFIG_SENSOR_I2C_SPEED;                       /* ddl@rock-chips.com : 100kHz */
    msg[1].read_type = 2;                             /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

    cnt = 3;
    err = -EAGAIN;
    while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 2);

        if (err >= 0) {
            *val = buf[0];
            return 0;
        } else {
        	SENSOR_TR("\n %s read reg(0x%x val:0x%x) failed, try to read again! \n",SENSOR_NAME_STRING(),reg, *val);
            udelay(10);
        }
    }

    return err;
}

/* write a array of registers  */
static int sensor_write_array(struct i2c_client *client, struct reginfo *regarray)
{
    int err = 0, cnt;
    int i = 0;
#if CONFIG_SENSOR_Focus
	struct sensor *sensor = to_sensor(client);
#endif
#if CONFIG_SENSOR_I2C_RDWRCHK
	char valchk;
#endif
#if CONFIG_SENSOR_WRITE_REGS	
	int j = 0, reg_num;
	u8 *ptemp, *phead;
	int reg_length;
#endif

	cnt = 0;
	if (sensor_task_lock(client, 1) < 0)
		goto sensor_write_array_end;
    while (regarray[i].reg != SEQUENCE_END) {
        
    	#if 0
    	if ((regarray == sensor_af_firmware) && (sensor->info_priv.enable == 0)) {
			SENSOR_DG("%s disable, Download af firmware terminated!\n",SENSOR_NAME_STRING());
			err = -EINVAL;
			goto sensor_write_array_end;
    	}
		#endif

#if CONFIG_SENSOR_WRITE_REGS
	
		j = i;		
		reg_num = 2;	
		reg_length = 0x0001;
						
		while((regarray[i].reg + reg_length) == regarray[i+1].reg) {		
			i++;			
			reg_num++;
			if(reg_num >= WRITE_REGS_NUM)
				break;
		}
		
		if(reg_num > 2) {			
			
			int size_num;
			size_num = reg_num + 1;
			
			ptemp = phead = (u8*)kmalloc((size_num+10)*sizeof(u8),GFP_KERNEL);
			if (!phead) {
				SENSOR_DG("-------------write registers allocate memory fail!!!\n");
                i = j;
                err = sensor_write(client, regarray[i].reg, regarray[i].val); 
			} else {			
    			*phead = regarray[j].reg >> 8;			
    			*(ptemp+1) = regarray[j].reg & 0xFF;
    			ptemp += 2;				
    			for( ; reg_num > 0; reg_num --, j++) {
    				*ptemp ++ = regarray[j].val;
    			}
    			
    			ptemp = phead;			
    			err = sensor_write_regs(client, ptemp,size_num);			
    			kfree(phead);	
			}
		}else{
			err = sensor_write(client, regarray[i].reg, regarray[i].val);
		}	
#else
		err = sensor_write(client, regarray[i].reg, regarray[i].val);        
#endif
        if (err < 0)
        {
            if (cnt-- > 0) {
			    SENSOR_TR("%s..write failed current reg:0x%x, Write array again !\n", SENSOR_NAME_STRING(),regarray[i].reg);
				i = 0;
				continue;
            } else {
                SENSOR_TR("%s..write array failed!!!\n", SENSOR_NAME_STRING());
                err = -EPERM;
				goto sensor_write_array_end;
            }
        } else {
        #if CONFIG_SENSOR_I2C_RDWRCHK
			sensor_read(client, regarray[i].reg, &valchk);
			if (valchk != regarray[i].val)
				SENSOR_TR("%s Reg:0x%x write(0x%x, 0x%x) fail\n",SENSOR_NAME_STRING(), regarray[i].reg, regarray[i].val, valchk);
		#endif
        }
        i++; 
    }

	#if CONFIG_SENSOR_Focus
	if (((regarray->reg == SEQUENCE_PROPERTY) && (regarray->val == SEQUENCE_INIT))
		|| (regarray == sensor_init_data)) {
		sensor->info_priv.affm_reinit = 1;
	}
	#endif

sensor_write_array_end:
	sensor_task_lock(client,0);
    return err;
}
#if CONFIG_SENSOR_I2C_RDWRCHK
static int sensor_readchk_array(struct i2c_client *client, struct reginfo *regarray)
{
    int cnt;
    int i = 0;
	char valchk;

	cnt = 0;
	valchk = 0;
    while (regarray[i].reg != 0)
    {
		sensor_read(client, regarray[i].reg, &valchk);
		if (valchk != regarray[i].val)
			SENSOR_TR("%s Reg:0x%x read(0x%x, 0x%x) error\n",SENSOR_NAME_STRING(), regarray[i].reg, regarray[i].val, valchk);

        i++;
    }
    return 0;
}
#endif
#if CONFIG_SENSOR_Focus
struct af_cmdinfo
{
	char cmd_tag;
	char cmd_para[4];
	char validate_bit;
};
static int sensor_af_cmdset(struct i2c_client *client, int cmd_main, struct af_cmdinfo *cmdinfo)
{

	return 0;
sensor_af_cmdset_err:
	return -1;
}

static int sensor_af_idlechk(struct i2c_client *client)
{
	int ret = 0;

    sensor_write(client,0x070A, 0x00);
    sensor_write(client,0x0700, 0x03);
    sensor_write(client,0x0701, 0xFF);	
    sensor_write(client,0x070C, 0x00);
    sensor_write(client,0x070C, 0x07);
    
sensor_af_idlechk_end:
	return ret;
}
static int sensor_af_touch_zone(struct i2c_client *client, int *zone_pos)
{
	int ret = 0;
    int xstart,ystart;
    
    *zone_pos += 1000;
    *(zone_pos+1) += 1000;
    *(zone_pos+2) += 1000;
    *(zone_pos+3) += 1000;

    xstart = ((*zone_pos + *(zone_pos+2))>>1)*800/2000;
    ystart = ((*(zone_pos+1) + *(zone_pos+3))>>1)*600/2000;

    if (xstart>0xa0) {
        xstart -= 0xa0;
    } else {
        xstart = 0;
    }

    if (ystart>0xa0) {
        ystart -= 0xa0;
    } else {
        ystart = 0;
    }
    
    ret |= sensor_write(client, AF_ZONE0_WEIGHT,0x01);
    ret |= sensor_write(client, AF_ZONE1_WEIGHT,0x00); 
    ret |= sensor_write(client, AF_ZONE2_WEIGHT,0x00);
    ret |= sensor_write(client, AF_ZONE3_WEIGHT,0x00);  
    ret |= sensor_write(client, AF_ZONE4_WEIGHT,0x00);	  
    ret |= sensor_write(client, AF_ZONE5_WEIGHT,0x00);
    ret |= sensor_write(client, AF_ZONE6_WEIGHT,0x00);    
    ret |= sensor_write(client, FACE_LC,0x03); 

    ret |= sensor_write(client, FACE_START_XH, ((xstart&0xff00)>>8));
    ret |= sensor_write(client, FACE_START_XL, xstart&0xff);
    ret |= sensor_write(client, FACE_START_YH, ((ystart&0xff00)>>8));
    ret |= sensor_write(client, FACE_START_YL, ystart&0xff);	

    ret |= sensor_write(client, FACE_SIZE_XH, 0x01);
	ret |= sensor_write(client, FACE_SIZE_XL, 0x40);
	ret |= sensor_write(client, FACE_SIZE_YH, 0x01);
	ret |= sensor_write(client, FACE_SIZE_YL, 0x40);

    SENSOR_DG("%s touch focus zone at (%d,%d)\n",SENSOR_NAME_STRING(), xstart,ystart);
    msleep(10);
sensor_af_zone_end:
    return ret;
}
static int sensor_af_single(struct i2c_client *client)
{
	int ret = 0;
	char state,cnt=0;
    struct sensor *sensor = to_sensor(client);
    
	ret |= sensor_write(client,AF_MODES_REG,0x03);
    ret |= sensor_write(client,AF_AUTOCMDS_REG,0x01);
    msleep(200);
    ret |= sensor_write(client,AF_AUTOCMDS_REG,0x02);

    do  {
        msleep(20);
        sensor_read(client,AF_FINISHCHK_REG,&state);
        cnt++;
    }while ((state != 1) && (cnt<100));

    if (state == 1) {
        sensor_read(client, AF_LENSPOS_REG_H,&sensor->parameter.af_pos[0]);
        sensor_read(client, AF_LENSPOS_REG_L,&sensor->parameter.af_pos[1]);
    }

    SENSOR_DG("%s single focus, state: %d cnt: %d\n",SENSOR_NAME_STRING(),state,cnt);
sensor_af_single_end:
	return ret;
}

static int sensor_af_const(struct i2c_client *client)
{
	int ret = 0;

    ret = sensor_write(client, AF_MODES_REG, 0x01);

    SENSOR_DG("%s const focus\n",SENSOR_NAME_STRING());
	return ret;
}
static int sensor_af_const_pause (struct i2c_client *client)
{
    int ret = 0;
    char status;
    struct sensor *sensor = to_sensor(client);
    
    sensor_read(client, 0x07ae, &status);

    if (status == 1) {
        sensor_read(client, AF_LENSPOS_REG_H,&sensor->parameter.af_pos[0]);
        sensor_read(client, AF_LENSPOS_REG_L,&sensor->parameter.af_pos[1]);
    } else {
        sensor_af_single(client);
    }

    SENSOR_DG("%s const focus pause\n",SENSOR_NAME_STRING());
    
}
static int sensor_af_special_pos (struct i2c_client *client)
{
    struct sensor *sensor = to_sensor(client);
    char status=0;
    
    sensor_write(client,0x070A, 0x00);
	sensor_write(client,0x0734, sensor->parameter.af_pos[0]& 0xFF);
	sensor_write(client,0x0735, sensor->parameter.af_pos[1] & 0xFF);
	sensor_write(client,0x070C, 0x00);
	mdelay(100);
    sensor_write(client,0x070C, 0x05);
    return 0;
}

static int sensor_af_init(struct i2c_client *client)
{
	int ret = 0;
	char state,cnt;

sensor_af_init_end:
	SENSOR_DG("%s %s ret:0x%x \n",SENSOR_NAME_STRING(),__FUNCTION__,ret);
	return ret;
}

static int sensor_af_downfirmware(struct i2c_client *client)
{
	struct sensor *sensor = to_sensor(client);
	int ret=0;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct v4l2_mbus_framefmt mf;
		
	SENSOR_DG("%s %s Enter\n",SENSOR_NAME_STRING(), __FUNCTION__);
    
	if (sensor_af_init(client)) {
		sensor->info_priv.funmodule_state &= (~SENSOR_AF_IS_OK);
		ret = -1;
	} else {
		sensor->info_priv.funmodule_state |= SENSOR_AF_IS_OK;
        
        mf.width = icd->user_width;
		mf.height = icd->user_height;
        mf.code = sensor->info_priv.fmt.code;
        mf.colorspace = sensor->info_priv.fmt.colorspace;
        mf.field	= V4L2_FIELD_NONE;
        
        if (sensor_fmt_videochk(NULL, &mf) == true) {    /* ddl@rock-chips.com: focus mode fix const auto focus in video */
            ret = sensor_af_const(client);
        } else {
    		switch (sensor->info_priv.auto_focus)
    		{
    			case SENSOR_AF_MODE_AUTO:
    			{
    				ret = sensor_af_single(client);
    				break;
    			}
    			case SENSOR_AF_MODE_CLOSE:
    			{
    				ret = 0;
    				break;
    			}
                case SENSOR_AF_MODE_CONTINUOUS:
                {
                    ret = sensor_af_const(client);
                    break;
                }
    			default:
                {
    				SENSOR_DG("%s focus mode(0x%x) is unkonwn\n",SENSOR_NAME_STRING(),sensor->info_priv.auto_focus);
                    goto sensor_af_downfirmware_end;
    			}
    		}
        }
		SENSOR_DG("%s sensor_af_downfirmware set focus mode(0x%x) ret:0x%x\n",SENSOR_NAME_STRING(), sensor->info_priv.auto_focus,ret);
	}

sensor_af_downfirmware_end:
	
	return ret;
}
static void sensor_af_workqueue(struct work_struct *work)
{
	struct sensor_work *sensor_work = container_of(work, struct sensor_work, dwork.work);
	struct i2c_client *client = sensor_work->client;
    struct sensor *sensor = to_sensor(client);
    struct af_cmdinfo cmdinfo;
    
    SENSOR_DG("%s %s Enter, cmd:0x%x \n",SENSOR_NAME_STRING(), __FUNCTION__,sensor_work->cmd);
    
    mutex_lock(&sensor->wq_lock);
    
    switch (sensor_work->cmd) 
    {
        case WqCmd_af_init:
        {
        	if (sensor_af_downfirmware(client) < 0) {
        		SENSOR_TR("%s Sensor_af_init is failed in sensor_af_workqueue!\n",SENSOR_NAME_STRING());
        	}            
            break;
        }
        case WqCmd_af_single:
        {
            sensor_af_touch_zone(client,sensor_work->zone_pos);
            
            if (sensor_af_single(client) < 0) {
        		SENSOR_TR("%s Sensor_af_single is failed in sensor_af_workqueue!\n",SENSOR_NAME_STRING());
                sensor_work->result = WqRet_fail;
        	} else {
                sensor_work->result = WqRet_success;
        	}
            break;
        }
        
        case WqCmd_af_special_pos:
        {            
			if (sensor_af_special_pos(client)< 0)
               sensor_work->result = WqRet_fail;
            else 
               sensor_work->result = WqRet_success;
            break;
        }
        #if 0 
        case WqCmd_af_near_pos:
        {            
            sensor_af_idlechk(client);
            cmdinfo.cmd_tag = StepFocus_Near_Tag;
            cmdinfo.validate_bit = 0x80;
			if (sensor_af_cmdset(client, StepMode_Cmd, &cmdinfo) < 0)
               sensor_work->result = WqRet_fail;
            else 
               sensor_work->result = WqRet_success;
            break;
        }
        case WqCmd_af_far_pos:
        {
            sensor_af_idlechk(client);
			cmdinfo.cmd_tag = StepFocus_Far_Tag;
			cmdinfo.validate_bit = 0x80;
			if (sensor_af_cmdset(client, StepMode_Cmd, &cmdinfo) < 0)
               sensor_work->result = WqRet_fail;
            else 
               sensor_work->result = WqRet_success;
            break;
        }
        #endif
        case WqCmd_af_continues:
        {
            if (sensor_af_const(client) < 0)
               sensor_work->result = WqRet_fail;
            else 
               sensor_work->result = WqRet_success;
            break;
        }
        case WqCmd_af_continues_pause:
        {
            if (sensor_af_const_pause(client) < 0)
               sensor_work->result = WqRet_fail;
            else 
               sensor_work->result = WqRet_success;
            break;
        }    
        case WqCmd_af_return_idle:
        {
            if (sensor_af_idlechk(client) < 0)
               sensor_work->result = WqRet_fail;
            else 
               sensor_work->result = WqRet_success;
            break;
        }  
        default:
            SENSOR_TR("Unknow command(%d) in %s af workqueue!",sensor_work->cmd,SENSOR_NAME_STRING());
            break;
    } 
set_end:
    if (sensor_work->wait == false) {
        kfree((void*)sensor_work);
    } else {
        wake_up(&sensor_work->done); 
    }
    mutex_unlock(&sensor->wq_lock); 
    return;
}

static int sensor_af_workqueue_set(struct soc_camera_device *icd, enum sensor_wq_cmd cmd, int var, bool wait, int *zone_pos)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct sensor *sensor = to_sensor(client); 
    struct sensor_work *wk;
    int ret=0;

    if (sensor->sensor_wq == NULL) { 
        ret = -EINVAL;
        goto sensor_af_workqueue_set_end;
    }

    if ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK) != SENSOR_AF_IS_OK) {
        if (cmd != WqCmd_af_init) {
            SENSOR_TR("%s %s cmd(%d) ingore,because af module isn't ready!",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
            ret = -1;
            goto sensor_af_workqueue_set_end;
        }
    }
    
    wk = kzalloc(sizeof(struct sensor_work), GFP_KERNEL);
    if (wk) {
	    wk->client = client;
	    INIT_WORK(&(wk->dwork.work), sensor_af_workqueue);
        wk->cmd = cmd;
        wk->result = WqRet_inval;
        wk->wait = wait;
        wk->var = var;
        init_waitqueue_head(&wk->done);

        if (zone_pos) {   
            if (*zone_pos || *(zone_pos+1) || *(zone_pos+2) || *(zone_pos+3)) {
                memcpy(wk->zone_pos,zone_pos, sizeof(wk->zone_pos));
            }
        }
        
	    queue_delayed_work(sensor->sensor_wq,&(wk->dwork),0);
        
        /* ddl@rock-chips.com: 
        * video_lock is been locked in v4l2_ioctl function, but auto focus may slow,
        * As a result any other ioctl calls will proceed very, very slowly since each call
        * will have to wait for the AF to finish. Camera preview is pause,because VIDIOC_QBUF 
        * and VIDIOC_DQBUF is sched. so unlock video_lock here.
        */
        if (wait == true) {
            mutex_unlock(&icd->video_lock);                     
            if (wait_event_timeout(wk->done, (wk->result != WqRet_inval), msecs_to_jiffies(2500)) == 0) {
                SENSOR_TR("%s %s cmd(%d) is timeout!",SENSOR_NAME_STRING(),__FUNCTION__,cmd);                        
            }
            ret = wk->result;
            kfree((void*)wk);
            mutex_lock(&icd->video_lock);  
        }
        
    } else {
        SENSOR_TR("%s %s cmd(%d) ingore,because struct sensor_work malloc failed!",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
        ret = -1;
    }
sensor_af_workqueue_set_end:
    return ret;
}
#endif
static int sensor_ioctrl(struct soc_camera_device *icd,enum rk29sensor_power_cmd cmd, int on)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	int ret = 0;

    SENSOR_DG("%s %s  cmd(%d) on(%d)\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd,on);

	switch (cmd)
	{
		#if 1 //derek
		case Sensor_PowerDown:
		{
			if (icl->powerdown) {
				ret = icl->powerdown(icd->pdev, on);
				//printk("=============================<<<<222RK30_PIN1_PB7's value is %d\n",gpio_get_value(RK30_PIN1_PB7));
				if (ret == RK29_CAM_IO_SUCCESS) {
					if (on == 0) {
						mdelay(2);
						if (icl->reset)
							icl->reset(icd->pdev);
						
					}
				} else if (ret == RK29_CAM_EIO_REQUESTFAIL) {
					ret = -ENODEV;
					goto sensor_power_end;
				}
			}
			break;
		}
		#endif
		case Sensor_Flash:
		{
			struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    		struct sensor *sensor = to_sensor(client);

			if (sensor->sensor_io_request && sensor->sensor_io_request->sensor_ioctrl) {
				sensor->sensor_io_request->sensor_ioctrl(icd->pdev,Cam_Flash, on);
                if(on){
                    //flash off after 2 secs
            		hrtimer_cancel(&(flash_off_timer.timer));
            		hrtimer_start(&(flash_off_timer.timer),ktime_set(0, 800*1000*1000),HRTIMER_MODE_REL);
                    }
			}
			break;
		}
		default:
		{
			SENSOR_TR("%s cmd(0x%x) is unknown!",SENSOR_NAME_STRING(),cmd);
			break;
		}
	}

sensor_power_end:
	return ret;
}

static enum hrtimer_restart flash_off_func(struct hrtimer *timer){
	struct flash_timer *fps_timer = container_of(timer, struct flash_timer, timer);
    sensor_ioctrl(fps_timer->icd,Sensor_Flash,0);
	SENSOR_DG("%s %s !!!!!!",SENSOR_NAME_STRING(),__FUNCTION__);
    return 0;
    
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl;
    const struct sensor_datafmt *fmt;
    char value;
    int ret,pid = 0;

    SENSOR_DG("\n%s..%s.. \n",SENSOR_NAME_STRING(),__FUNCTION__);
#if 1 //derek
	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
//if (sensor_ioctrl(icd, Sensor_PowerDown, 1) < 0) {
		ret = -ENODEV;
		goto sensor_INIT_ERR;
	}
#endif
    /* soft reset */
	if (sensor_task_lock(client,1)<0)
		goto sensor_INIT_ERR;
  /* 
    ret = sensor_write(client, 0x3008, 0x80);
    if (ret != 0) {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_INIT_ERR;
    }
*/
    mdelay(5);  //delay 5 microseconds
	/* check if it is an sensor sensor */

    ret = sensor_read(client, 0x0000, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id high byte failed\n");
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }
    printk("jzw 5065:value=0x%02x\n",value);
    pid |= (value << 8);

    ret = sensor_read(client, 0x0001, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id low byte failed\n");
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }
     printk("jzw 506522:value=0x%02x\n",value);
    pid |= (value & 0xff);
    SENSOR_DG("\n %s  pid = 0x%x \n", SENSOR_NAME_STRING(), pid);

    if (pid == SENSOR_ID) {
        sensor->model = SENSOR_V4L2_IDENT;
    } else {
        SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }

    // 写两次init_date，尽量避免打开摄像头绿屏的问题，Edit by zhansb@20120524
    SENSOR_DG("write 1 time\n");
    ret = sensor_write_array(client, sensor_init_data);
    if (ret != 0) {
        SENSOR_TR("error: %s initial failed\n",SENSOR_NAME_STRING());
        goto sensor_INIT_ERR;
    }
     msleep(200);
    ret = sensor_write_array(client, sensor_init_data1);
    if (ret != 0) {
        SENSOR_TR("error: %s initial1  failed\n",SENSOR_NAME_STRING());
        goto sensor_INIT_ERR;
    }
     msleep(200);
    ret = sensor_write_array(client, sensor_init_data2);
    if (ret != 0) {
        SENSOR_TR("error: %s initial failed\n",SENSOR_NAME_STRING());
        goto sensor_INIT_ERR;
    }
	 msleep(200);
//   sensor_write(client, 0x070a, 0x01);	 
    ret = sensor_read(client, 0x0010, &value);
    if (ret != 0) {
        ret = -ENODEV;
        SENSOR_TR("error: %s initial failed\n",SENSOR_NAME_STRING());		
        goto sensor_INIT_ERR;
    }
	printk("<<<<addr 0010 's value is %d\n",value);



	sensor_task_lock(client,0);
    sensor->info_priv.winseqe_cur_addr  = SENSOR_INIT_WINSEQADR;
	fmt = sensor_find_datafmt(SENSOR_INIT_PIXFMT,sensor_colour_fmts, ARRAY_SIZE(sensor_colour_fmts));
    if (!fmt) {
        SENSOR_TR("error: %s initial array colour fmts is not support!!",SENSOR_NAME_STRING());
        ret = -EINVAL;
        goto sensor_INIT_ERR;
    }
	sensor->info_priv.fmt = *fmt;

#if 1
    /* sensor sensor information for initialization  */
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
	if (qctrl)
    	sensor->info_priv.whiteBalance = qctrl->default_value;

	// 解决进摄像头后第一次拍的照片跟后续的拍的照片白平衡相差较大的问题，add by zhansb@120614
#if CONFIG_SENSOR_WhiteBalance
    sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
#endif
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_BRIGHTNESS);
	if (qctrl)
    	sensor->info_priv.brightness = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
	if (qctrl)
    	sensor->info_priv.effect = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EXPOSURE);
	if (qctrl)
        sensor->info_priv.exposure = qctrl->default_value;

	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_SATURATION);
	if (qctrl)
        sensor->info_priv.saturation = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_CONTRAST);
	if (qctrl)
        sensor->info_priv.contrast = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_HFLIP);
	if (qctrl)
        sensor->info_priv.mirror = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_VFLIP);
	if (qctrl)
        sensor->info_priv.flip = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_SCENE);
	if (qctrl)
        sensor->info_priv.scene = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_ZOOM_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.digitalzoom = qctrl->default_value;

    /* ddl@rock-chips.com : if sensor support auto focus and flash, programer must run focus and flash code  */
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.focus = qctrl->default_value;

	#if CONFIG_SENSOR_Flash
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FLASH);
	if (qctrl)
        sensor->info_priv.flash = qctrl->default_value;
    flash_off_timer.icd = icd;
	flash_off_timer.timer.function = flash_off_func;
    #endif
    SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),((val == 0)?__FUNCTION__:"sensor_reinit"),icd->user_width,icd->user_height);

    sensor->info_priv.funmodule_state = SENSOR_INIT_IS_OK;
#endif     
    return 0;
sensor_INIT_ERR:
    sensor->info_priv.funmodule_state &= ~SENSOR_INIT_IS_OK;
	sensor_task_lock(client,0);
	sensor_deactivate(client); 
	printk("init error============================================================\n");
    return ret;
}
static int sensor_deactivate(struct i2c_client *client)
{
	struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
    
	SENSOR_DG("\n%s..%s.. Enter\n",SENSOR_NAME_STRING(),__FUNCTION__);

	/* ddl@rock-chips.com : all sensor output pin must change to input for other sensor */
    if (sensor->info_priv.funmodule_state & SENSOR_INIT_IS_OK) {
    	
    } 
    #if 1 //derek
        sensor_ioctrl(icd, Sensor_PowerDown, 1);
    #endif
    
    msleep(100); 
	/* ddl@rock-chips.com : sensor config init width , because next open sensor quickly(soc_camera_open -> Try to configure with default parameters) */
	icd->user_width = SENSOR_INIT_WIDTH;
    icd->user_height = SENSOR_INIT_HEIGHT;
    sensor->info_priv.funmodule_state &= ~SENSOR_INIT_IS_OK;
	return 0;
}

static  struct reginfo sensor_power_down_sequence[]=
{
    {0x00,0x00}
};

static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
    int ret;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if (pm_msg.event == PM_EVENT_SUSPEND) {
        SENSOR_DG("\n %s Enter Suspend.. \n", SENSOR_NAME_STRING());
        ret = sensor_write_array(client, sensor_power_down_sequence) ;
        if (ret != 0) {
            SENSOR_TR("\n %s..%s WriteReg Fail.. \n", SENSOR_NAME_STRING(),__FUNCTION__);
            return ret;
        } else {
        	#if 0 //derek
            ret = sensor_ioctrl(icd, Sensor_PowerDown, 1);
            if (ret < 0) {
			    SENSOR_TR("\n %s suspend fail for turn on power!\n", SENSOR_NAME_STRING());
                return -EINVAL;
            }
            #endif
        }
    } else {
        SENSOR_TR("\n %s cann't suppout Suspend..\n",SENSOR_NAME_STRING());
        return -EINVAL;
    }

    return 0;
}

static int sensor_resume(struct soc_camera_device *icd)
{
	int ret;
#if 0 //derek
    ret = sensor_ioctrl(icd, Sensor_PowerDown, 0);
    if (ret < 0) {
		SENSOR_TR("\n %s resume fail for turn on power!\n", SENSOR_NAME_STRING());
        return -EINVAL;
    }
#endif
	SENSOR_DG("\n %s Enter Resume.. \n", SENSOR_NAME_STRING());
	return 0;
}

static int sensor_set_bus_param(struct soc_camera_device *icd,
                                unsigned long flags)
{

    return 0;
}

static unsigned long sensor_query_bus_param(struct soc_camera_device *icd)
{
    struct soc_camera_link *icl = to_soc_camera_link(icd);
    unsigned long flags = SENSOR_BUS_PARAM;

    return soc_camera_apply_sensor_flags(icl, flags);
}

static int sensor_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);

    mf->width	= icd->user_width;
	mf->height	= icd->user_height;
	mf->code	= sensor->info_priv.fmt.code;
	mf->colorspace	= sensor->info_priv.fmt.colorspace;
	mf->field	= V4L2_FIELD_NONE;

    return 0;
}
static bool sensor_fmt_capturechk(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{   //capture mode
    bool ret = false;

	if ((mf->width == 1024) && (mf->height == 768)) {
		ret = true;
	} else if ((mf->width == 1280) && (mf->height == 1024)) {
		ret = true;
	} else if ((mf->width == 1600) && (mf->height == 1200)) {
		ret = true;
	} else if ((mf->width == 2048) && (mf->height == 1536)) {
		ret = true;
	} else if ((mf->width == 2592) && (mf->height == 1944)) {
		ret = true;
	}

	if (ret == true)
		SENSOR_DG("%s %dx%d is capture format\n", __FUNCTION__, mf->width, mf->height);
	return ret;
}

static bool sensor_fmt_videochk(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{  // video mode
    bool ret = false;

	if ((mf->width == 1280) && (mf->height == 720)) {
		ret = true;
	} else if ((mf->width == 1920) && (mf->height == 1080)) {
		ret = true;
	}

	if (ret == true)
		SENSOR_DG("%s %dx%d is video format\n", __FUNCTION__, mf->width, mf->height);
	return ret;
}
static int sensor_s_fmt(struct v4l2_subdev *sd,struct v4l2_mbus_framefmt *mf)
{
	 char value,af_status=0,af_pos_h=0,af_pos_l=0;
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    const struct sensor_datafmt *fmt;
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl;
	struct soc_camera_device *icd = client->dev.platform_data;
    struct reginfo *winseqe_set_addr=NULL;
    int ret = 0, set_w,set_h;
	int i=0;

    
	fmt = sensor_find_datafmt(mf->code, sensor_colour_fmts,
				   ARRAY_SIZE(sensor_colour_fmts));
	if (!fmt) {
        ret = -EINVAL;
        goto sensor_s_fmt_end;
    }

	if (sensor->info_priv.fmt.code != mf->code) {
		switch (mf->code)
		{
			case V4L2_MBUS_FMT_YUYV8_2X8:
			{
				winseqe_set_addr = sensor_ClrFmt_YUYV;
				break;
			}
			case V4L2_MBUS_FMT_UYVY8_2X8:
			{
				winseqe_set_addr = sensor_ClrFmt_UYVY;
				break;
			}
			default:
				break;
		}
		if (winseqe_set_addr != NULL) {
            sensor_write_array(client, winseqe_set_addr);
			sensor->info_priv.fmt.code = mf->code;
            sensor->info_priv.fmt.colorspace= mf->colorspace;            
			SENSOR_DG("%s v4l2_mbus_code:%d set success!\n", SENSOR_NAME_STRING(),mf->code);
		} else {
			SENSOR_TR("%s v4l2_mbus_code:%d is invalidate!\n", SENSOR_NAME_STRING(),mf->code);
		}
	}

    set_w = mf->width;
    set_h = mf->height;    
				
	if (((set_w <= 176) && (set_h <= 144)) && (sensor_qcif[0].reg!=SEQUENCE_END))
	{
		winseqe_set_addr = sensor_qcif;
        set_w = 176;
        set_h = 144;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && (sensor_qvga[0].reg!=SEQUENCE_END))
    {
        winseqe_set_addr = sensor_qvga;
        set_w = 320;
        set_h = 240;
    }
    else if (((set_w <= 352) && (set_h<= 288)) && (sensor_cif[0].reg!=SEQUENCE_END))
    {
        winseqe_set_addr = sensor_cif;
        set_w = 352;
        set_h = 288;
    }
    else if (((set_w <= 640) && (set_h <= 480)) && (sensor_vga[0].reg!=SEQUENCE_END))
    {
        winseqe_set_addr = sensor_vga;
        set_w = 640;
        set_h = 480;
    }
    else if (((set_w <= 800) && (set_h <= 600)) && (sensor_svga[0].reg!=SEQUENCE_END))
    {   
        winseqe_set_addr = sensor_svga;
        set_w = 800;
        set_h = 600;
    }
	else if (((set_w <= 1024) && (set_h <= 768)) && (sensor_xga[0].reg!=SEQUENCE_END))
    {
        winseqe_set_addr = sensor_xga;
        set_w = 1024;
        set_h = 768;
    }
	else if (((set_w <= 1280) && (set_h <= 720)) && (sensor_720p[0].reg!=SEQUENCE_END))
    {
        winseqe_set_addr = sensor_720p;
        set_w = 1280;
        set_h = 720;
    }
    else if (((set_w <= 1280) && (set_h <= 1024)) && (sensor_sxga[0].reg!=SEQUENCE_END))
    {
        winseqe_set_addr = sensor_sxga;
        set_w = 1280;
        set_h = 1024;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && (sensor_uxga[0].reg!=SEQUENCE_END))
    {
        winseqe_set_addr = sensor_uxga;
        set_w = 1600;
        set_h = 1200;
    }
    else if (((set_w <= 1920) && (set_h <= 1080)) && (sensor_1080p[0].reg!=SEQUENCE_END))
    {
        winseqe_set_addr = sensor_1080p;
        set_w = 1920;
        set_h = 1080;
    }
	else if (((set_w <= 2048) && (set_h <= 1536)) && (sensor_qxga[0].reg!=SEQUENCE_END))
    {
        winseqe_set_addr = sensor_qxga;
        set_w = 2048;
        set_h = 1536;
    }
	else if (((set_w <= 2592) && (set_h <= 1944)) && (sensor_qsxga[0].reg!=SEQUENCE_END))
    {
    	
        winseqe_set_addr = sensor_qsxga;
        set_w = 2592;
        set_h = 1944;
    }
    else
    {
        winseqe_set_addr = SENSOR_INIT_WINSEQADR;               /* ddl@rock-chips.com : Sensor output smallest size if  isn't support app  */
        set_w = SENSOR_INIT_WIDTH;
        set_h = SENSOR_INIT_HEIGHT;
		SENSOR_TR("\n %s..%s Format is Invalidate. pix->width = %d.. pix->height = %d\n",SENSOR_NAME_STRING(),__FUNCTION__,mf->width,mf->height);
    }
    
    if (winseqe_set_addr  != sensor->info_priv.winseqe_cur_addr)
    {
		if (sensor_fmt_capturechk(sd,mf) == true) {					/* ddl@rock-chips.com : Capture */
        
		#if CONFIG_SENSOR_Flash
            if ((sensor->info_priv.flash == 1) || (sensor->info_priv.flash == 2)) {
                sensor_ioctrl(icd, Sensor_Flash, Flash_On);
                printk("%s flash on in capture!\n", SENSOR_NAME_STRING());
            }
        #endif
		}else {                                        /* ddl@rock-chips.com : Video */
		#if CONFIG_SENSOR_Flash 
            if ((sensor->info_priv.flash == 1) || (sensor->info_priv.flash == 2)) {
                sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
                printk("%s flash off in preivew!\n", SENSOR_NAME_STRING());
            }
        #endif
        }

        #if CONFIG_SENSOR_Focus
            if (sensor->info_priv.auto_focus == SENSOR_AF_MODE_CONTINUOUS) {
                sensor_af_workqueue_set(icd,WqCmd_af_continues_pause,0,true,NULL);    
            }
        #endif            
        
		if ((sensor->info_priv.winseqe_cur_addr->reg == SEQUENCE_PROPERTY) && (sensor->info_priv.winseqe_cur_addr->val == SEQUENCE_INIT)) {
			if (((winseqe_set_addr->reg == SEQUENCE_PROPERTY) && (winseqe_set_addr->val == SEQUENCE_NORMAL))
				|| (winseqe_set_addr->reg != SEQUENCE_PROPERTY)) {
				ret |= sensor_write_array(client,sensor_init_data);
				SENSOR_DG("\n%s reinit ret:0x%x \n",SENSOR_NAME_STRING(), ret);
			}
		}
		
        ret |= sensor_write_array(client, winseqe_set_addr);
        if (ret != 0) {
            SENSOR_TR("%s set format capability failed\n", SENSOR_NAME_STRING());
            #if CONFIG_SENSOR_Flash
            if (sensor_fmt_capturechk(sd,mf) == true) {
                if ((sensor->info_priv.flash == 1) || (sensor->info_priv.flash == 2)) {
                    sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
                    SENSOR_TR("%s Capture format set fail, flash off !\n", SENSOR_NAME_STRING());
                }
            }
            #endif
            goto sensor_s_fmt_end;
        }
	    sensor->info_priv.winseqe_cur_addr  = winseqe_set_addr;
		if (sensor_fmt_capturechk(sd,mf) == true) {				    /* ddl@rock-chips.com : Capture */
        #if CONFIG_SENSOR_Focus
            sensor_af_workqueue_set(icd,WqCmd_af_special_pos,0,true,NULL);
        #endif
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);
			if (sensor->info_priv.whiteBalance != 0) {
				qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
				sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
			}
			sensor->info_priv.snap2preview = true;
		} else if (sensor_fmt_videochk(sd,mf) == true) {			/* ddl@rock-chips.com : Video */
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);

			sensor->info_priv.video2preview = true;
			
		} else if ((sensor->info_priv.snap2preview == true) || (sensor->info_priv.video2preview == true)) {
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);

			if (sensor->info_priv.snap2preview == true) {
				qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
				sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
			}
            #if CONFIG_SENSOR_Focus
            if (sensor->info_priv.auto_focus == SENSOR_AF_MODE_AUTO) {
                sensor_af_workqueue_set(icd,WqCmd_af_return_idle,0,false,NULL);    
            } else if (sensor->info_priv.auto_focus == SENSOR_AF_MODE_CONTINUOUS) {
                sensor_af_workqueue_set(icd,WqCmd_af_continues,0,false,NULL);    
            }
            #else   
            msleep(500);
            #endif
			sensor->info_priv.video2preview = false;
			sensor->info_priv.snap2preview = false;
		}
        SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),__FUNCTION__,set_w,set_h);
    }
    else
    {			// preview size @ video preview 
        SENSOR_DG("\n %s .. Current Format is validate. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),set_w,set_h);
    }
  
	mf->width = set_w;
	mf->height = set_h;
sensor_s_fmt_end:
    return ret;
}

static int sensor_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
    const struct sensor_datafmt *fmt;
    int ret = 0,set_w,set_h;
   
	fmt = sensor_find_datafmt(mf->code, sensor_colour_fmts,
				   ARRAY_SIZE(sensor_colour_fmts));
	if (fmt == NULL) {
		fmt = &sensor->info_priv.fmt;
        mf->code = fmt->code;
	} 

    if (mf->height > SENSOR_MAX_HEIGHT)
        mf->height = SENSOR_MAX_HEIGHT;
    else if (mf->height < SENSOR_MIN_HEIGHT)
        mf->height = SENSOR_MIN_HEIGHT;

    if (mf->width > SENSOR_MAX_WIDTH)
        mf->width = SENSOR_MAX_WIDTH;
    else if (mf->width < SENSOR_MIN_WIDTH)
        mf->width = SENSOR_MIN_WIDTH;

    set_w = mf->width;
    set_h = mf->height;

	if (((set_w <= 176) && (set_h <= 144)) && (sensor_qcif[0].reg!=SEQUENCE_END))
	{
        set_w = 176;
        set_h = 144;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && (sensor_qvga[0].reg!=SEQUENCE_END))
    {
        set_w = 320;
        set_h = 240;
    }
    else if (((set_w <= 352) && (set_h<= 288)) && (sensor_cif[0].reg!=SEQUENCE_END))
    {
        set_w = 352;
        set_h = 288;
    }
    else if (((set_w <= 640) && (set_h <= 480)) && (sensor_vga[0].reg!=SEQUENCE_END))
    {
        set_w = 640;
        set_h = 480;
    }
    else if (((set_w <= 800) && (set_h <= 600)) && (sensor_svga[0].reg!=SEQUENCE_END))
    {
        set_w = 800;
        set_h = 600;
    }
	else if (((set_w <= 1024) && (set_h <= 768)) && (sensor_xga[0].reg!=SEQUENCE_END))
    {
        set_w = 1024;
        set_h = 768;
    }
	else if (((set_w <= 1280) && (set_h <= 720)) && (sensor_720p[0].reg!=SEQUENCE_END))
    {
        set_w = 1280;
        set_h = 720;
    }
    else if (((set_w <= 1280) && (set_h <= 1024)) && (sensor_sxga[0].reg!=SEQUENCE_END))
    {
        set_w = 1280;
        set_h = 1024;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && (sensor_uxga[0].reg!=SEQUENCE_END))
    {
        set_w = 1600;
        set_h = 1200;
    }
    else if (((set_w <= 1920) && (set_h <= 1080)) && (sensor_1080p[0].reg!=SEQUENCE_END))
    {
        set_w = 1920;
        set_h = 1080;
    }
	else if (((set_w <= 2048) && (set_h <= 1536)) && (sensor_qxga[0].reg!=SEQUENCE_END))
    {
        set_w = 2048;
        set_h = 1536;
    }
	else if (((set_w <= 2592) && (set_h <= 1944)) && (sensor_qsxga[0].reg!=SEQUENCE_END))
    {
        set_w = 2592;
        set_h = 1944;
    }
    else
    {
        set_w = SENSOR_INIT_WIDTH;
        set_h = SENSOR_INIT_HEIGHT;
    }

    mf->width = set_w;
    mf->height = set_h;
    
    mf->colorspace = fmt->colorspace;
    
    return ret;
}

 static int sensor_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *id)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);

    if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
        return -EINVAL;

    if (id->match.addr != client->addr)
        return -ENODEV;

    id->ident = SENSOR_V4L2_IDENT;      /* ddl@rock-chips.com :  Return OV2655  identifier */
    id->revision = 0;

    return 0;
}
#if CONFIG_SENSOR_Brightness
static int sensor_set_brightness(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_BrightnessSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_BrightnessSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Effect
static int sensor_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	
    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_EffectSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_EffectSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n%s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Exposure
static int sensor_set_exposure(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_ExposureSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_ExposureSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Saturation
static int sensor_set_saturation(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_SaturationSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_SaturationSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Contrast
static int sensor_set_contrast(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_ContrastSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_ContrastSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Mirror
static int sensor_set_mirror(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_MirrorSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_MirrorSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Flip
static int sensor_set_flip(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_FlipSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_FlipSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Scene
static int sensor_set_scene(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_SceneSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_SceneSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_WhiteBalance
static int sensor_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_WhiteBalanceSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_WhiteBalanceSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_DigitalZoom
static int sensor_set_digitalzoom(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int *value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl_info;
    int digitalzoom_cur, digitalzoom_total;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_ZOOM_ABSOLUTE);
	if (qctrl_info)
		return -EINVAL;

    digitalzoom_cur = sensor->info_priv.digitalzoom;
    digitalzoom_total = qctrl_info->maximum;

    if ((*value > 0) && (digitalzoom_cur >= digitalzoom_total))
    {
        SENSOR_TR("%s digitalzoom is maximum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if  ((*value < 0) && (digitalzoom_cur <= qctrl_info->minimum))
    {
        SENSOR_TR("%s digitalzoom is minimum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if ((*value > 0) && ((digitalzoom_cur + *value) > digitalzoom_total))
    {
        *value = digitalzoom_total - digitalzoom_cur;
    }

    if ((*value < 0) && ((digitalzoom_cur + *value) < 0))
    {
        *value = 0 - digitalzoom_cur;
    }

    digitalzoom_cur += *value;

    if (sensor_ZoomSeqe[digitalzoom_cur] != NULL)
    {
        if (sensor_write_array(client, sensor_ZoomSeqe[digitalzoom_cur]) != 0)
        {
            SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
            return -EINVAL;
        }
        SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, *value);
        return 0;
    }

    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Focus
static int sensor_set_focus_absolute(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl_info;
	int ret = 0;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (!qctrl_info)
		return -EINVAL;
    
	if ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK) && (sensor->info_priv.affm_reinit == 0)) {
		if ((value >= qctrl_info->minimum) && (value <= qctrl_info->maximum)) {
            ret = sensor_af_workqueue_set(icd, WqCmd_af_special_pos, value, true,NULL);
			SENSOR_DG("%s..%s : %d  ret:0x%x\n",SENSOR_NAME_STRING(),__FUNCTION__, value,ret);
		} else {
			ret = -EINVAL;
			SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
		}
	} else {
		ret = -EACCES;
		SENSOR_TR("\n %s..%s AF module state(0x%x, 0x%x) is error!\n",SENSOR_NAME_STRING(),__FUNCTION__,
			sensor->info_priv.funmodule_state,sensor->info_priv.affm_reinit);
	}

	return ret;
}
static int sensor_set_focus_relative(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl_info;
	int ret = 0;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_RELATIVE);
	if (!qctrl_info)
		return -EINVAL;    

	if ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK) && (sensor->info_priv.affm_reinit == 0)) {
		if ((value >= qctrl_info->minimum) && (value <= qctrl_info->maximum)) {            
            if (value > 0) {
                ret = sensor_af_workqueue_set(icd, WqCmd_af_near_pos, 0, true,NULL);
            } else {
                ret = sensor_af_workqueue_set(icd, WqCmd_af_far_pos, 0, true,NULL);
            }
			SENSOR_DG("%s..%s : %d  ret:0x%x\n",SENSOR_NAME_STRING(),__FUNCTION__, value,ret);
		} else {
			ret = -EINVAL;
			SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
		}
	} else {
		ret = -EACCES;
		SENSOR_TR("\n %s..%s AF module state(0x%x, 0x%x) is error!\n",SENSOR_NAME_STRING(),__FUNCTION__,
			sensor->info_priv.funmodule_state,sensor->info_priv.affm_reinit);
	}
	return ret;
}

static int sensor_set_focus_mode(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value, int *zone_pos)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct sensor *sensor = to_sensor(client);
	int ret = 0;
    
	if ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK)  && (sensor->info_priv.affm_reinit == 0)) {
		switch (value)
		{
			case SENSOR_AF_MODE_AUTO:
			{
				ret = sensor_af_workqueue_set(icd, WqCmd_af_single, 0, true, zone_pos);
				break;
			}

			/*case SENSOR_AF_MODE_MACRO:
			{
				ret = sensor_set_focus_absolute(icd, qctrl, 0xff);
				break;
			}

			case SENSOR_AF_MODE_INFINITY:
			{
				ret = sensor_set_focus_absolute(icd, qctrl, 0x00);
				break;
			}
			*/
			case SENSOR_AF_MODE_CONTINUOUS:
			{
				ret = sensor_af_workqueue_set(icd, WqCmd_af_continues, 0, true,NULL);
				break;
			}
			default:
				SENSOR_TR("\n %s..%s AF value(0x%x) is error!\n",SENSOR_NAME_STRING(),__FUNCTION__,value);
				break;

		}

		SENSOR_DG("%s..%s : %d  ret:0x%x\n",SENSOR_NAME_STRING(),__FUNCTION__, value,ret);
	} else {
		ret = -EACCES;
		SENSOR_TR("\n %s..%s AF module state(0x%x, 0x%x) is error!\n",SENSOR_NAME_STRING(),__FUNCTION__,
			sensor->info_priv.funmodule_state,sensor->info_priv.affm_reinit);
	}

	return ret;
}
#endif
#if CONFIG_SENSOR_Flash
static int sensor_set_flash(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{    
    if ((value >= qctrl->minimum) && (value <= qctrl->maximum)) {
        if (value == 3) {       /* ddl@rock-chips.com: torch */
            sensor_ioctrl(icd, Sensor_Flash, Flash_Torch);   /* Flash On */
        } else {
            sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
        }
        SENSOR_DG("%s..%s : %d\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
        return 0;
    }
    
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
static int sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
    const struct v4l2_queryctrl *qctrl;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
        return -EINVAL;
    }

    switch (ctrl->id)
    {
        case V4L2_CID_BRIGHTNESS:
            {
                ctrl->value = sensor->info_priv.brightness;
                break;
            }
        case V4L2_CID_SATURATION:
            {
                ctrl->value = sensor->info_priv.saturation;
                break;
            }
        case V4L2_CID_CONTRAST:
            {
                ctrl->value = sensor->info_priv.contrast;
                break;
            }
        case V4L2_CID_DO_WHITE_BALANCE:
            {
                ctrl->value = sensor->info_priv.whiteBalance;
                break;
            }
        case V4L2_CID_EXPOSURE:
            {
                ctrl->value = sensor->info_priv.exposure;
                break;
            }
        case V4L2_CID_HFLIP:
            {
                ctrl->value = sensor->info_priv.mirror;
                break;
            }
        case V4L2_CID_VFLIP:
            {
                ctrl->value = sensor->info_priv.flip;
                break;
            }
        default :
                break;
    }
    return 0;
}



static int sensor_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;
    const struct v4l2_queryctrl *qctrl;


    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
        return -EINVAL;
    }

    switch (ctrl->id)
    {
#if CONFIG_SENSOR_Brightness
        case V4L2_CID_BRIGHTNESS:
            {
                if (ctrl->value != sensor->info_priv.brightness)
                {
                    if (sensor_set_brightness(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.brightness = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Exposure
        case V4L2_CID_EXPOSURE:
            {
                if (ctrl->value != sensor->info_priv.exposure)
                {
                    if (sensor_set_exposure(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.exposure = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Saturation
        case V4L2_CID_SATURATION:
            {
                if (ctrl->value != sensor->info_priv.saturation)
                {
                    if (sensor_set_saturation(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.saturation = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Contrast
        case V4L2_CID_CONTRAST:
            {
                if (ctrl->value != sensor->info_priv.contrast)
                {
                    if (sensor_set_contrast(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.contrast = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_WhiteBalance
        case V4L2_CID_DO_WHITE_BALANCE:
            {
                if (ctrl->value != sensor->info_priv.whiteBalance)
                {
                    if (sensor_set_whiteBalance(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.whiteBalance = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Mirror
        case V4L2_CID_HFLIP:
            {
                if (ctrl->value != sensor->info_priv.mirror)
                {
                    if (sensor_set_mirror(icd, qctrl,ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.mirror = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Flip
        case V4L2_CID_VFLIP:
            {
                if (ctrl->value != sensor->info_priv.flip)
                {
                    if (sensor_set_flip(icd, qctrl,ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.flip = ctrl->value;
                }
                break;
            }
#endif
        default:
            break;
    }

    return 0;
}
static int sensor_g_ext_control(struct soc_camera_device *icd , struct v4l2_ext_control *ext_ctrl)
{
    const struct v4l2_queryctrl *qctrl;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);

    qctrl = soc_camera_find_qctrl(&sensor_ops, ext_ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
        return -EINVAL;
    }

    switch (ext_ctrl->id)
    {
        case V4L2_CID_SCENE:
            {
                ext_ctrl->value = sensor->info_priv.scene;
                break;
            }
        case V4L2_CID_EFFECT:
            {
                ext_ctrl->value = sensor->info_priv.effect;
                break;
            }
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                ext_ctrl->value = sensor->info_priv.digitalzoom;
                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FOCUS_ABSOLUTE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FOCUS_RELATIVE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FLASH:
            {
                ext_ctrl->value = sensor->info_priv.flash;
                break;
            }
        default :
            break;
    }
    return 0;
}
static int sensor_s_ext_control(struct soc_camera_device *icd, struct v4l2_ext_control *ext_ctrl)
{
    const struct v4l2_queryctrl *qctrl;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);
    int val_offset,ret;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ext_ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = 0x%x  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
        return -EINVAL;
    }

	val_offset = 0;
    switch (ext_ctrl->id)
    {
#if CONFIG_SENSOR_Scene
        case V4L2_CID_SCENE:
            {
                if (ext_ctrl->value != sensor->info_priv.scene)
                {
                    if (sensor_set_scene(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.scene = ext_ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Effect
        case V4L2_CID_EFFECT:
            {                
                if (ext_ctrl->value != sensor->info_priv.effect)
                {                    
                    if (sensor_set_effect(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.effect= ext_ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_DigitalZoom
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;

                if (ext_ctrl->value != sensor->info_priv.digitalzoom)
                {
                    val_offset = ext_ctrl->value -sensor->info_priv.digitalzoom;

                    if (sensor_set_digitalzoom(icd, qctrl,&val_offset) != 0)
                        return -EINVAL;
                    sensor->info_priv.digitalzoom += val_offset;

                    SENSOR_DG("%s digitalzoom is %x\n",SENSOR_NAME_STRING(),  sensor->info_priv.digitalzoom);
                }

                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                if (ext_ctrl->value)
                {
                    if (sensor_set_digitalzoom(icd, qctrl,&ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.digitalzoom += ext_ctrl->value;

                    SENSOR_DG("%s digitalzoom is %x\n", SENSOR_NAME_STRING(), sensor->info_priv.digitalzoom);
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Focus
        case V4L2_CID_FOCUS_ABSOLUTE:
            {
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;

                break;
            }
        case V4L2_CID_FOCUS_RELATIVE:
            {
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;

                sensor_set_focus_relative(icd, qctrl,ext_ctrl->value);
                break;
            }
		case V4L2_CID_FOCUS_AUTO:
			{
                printk("AF: %d\n",ext_ctrl->value);
				if (ext_ctrl->value) {
                    if ((ext_ctrl->value==1) || (SENSOR_AF_MODE_AUTO == sensor->info_priv.auto_focus)) {
    					if (sensor_set_focus_mode(icd, qctrl,SENSOR_AF_MODE_AUTO,ext_ctrl->rect) != 0) {
    						if(0 == (sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK)) {
    							sensor->info_priv.auto_focus = SENSOR_AF_MODE_AUTO;
    						}
    						return -EINVAL;
    					}
                    }
                    if (ext_ctrl->value == 1)
					    sensor->info_priv.auto_focus = SENSOR_AF_MODE_AUTO;
				} else if (SENSOR_AF_MODE_AUTO == sensor->info_priv.auto_focus){
					if (ext_ctrl->value == 0)
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_CLOSE;
				} 
				break;
			}
		case V4L2_CID_FOCUS_CONTINUOUS:
			{
				if (SENSOR_AF_MODE_CONTINUOUS != sensor->info_priv.auto_focus) {
					if (ext_ctrl->value == 1) {
						if (sensor_set_focus_mode(icd, qctrl,SENSOR_AF_MODE_CONTINUOUS,NULL) != 0) {
    						if(0 == (sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK)) {
    							sensor->info_priv.auto_focus = SENSOR_AF_MODE_CONTINUOUS;
    						}
    						return -EINVAL;
						}
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_CONTINUOUS;
					}
				} else {
					if (ext_ctrl->value == 0)
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_CLOSE;
				}
				break;
			}
#endif
#if CONFIG_SENSOR_Flash
        case V4L2_CID_FLASH:
            {
                if (sensor_set_flash(icd, qctrl,ext_ctrl->value) != 0)
                    return -EINVAL;
                sensor->info_priv.flash = ext_ctrl->value;

                SENSOR_DG("%s flash is %x\n",SENSOR_NAME_STRING(), sensor->info_priv.flash);
                break;
            }
#endif
        default:
            break;
    }

    return 0;
}

static int sensor_g_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    int i, error_cnt=0, error_idx=-1;


    for (i=0; i<ext_ctrl->count; i++) {
        if (sensor_g_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}

static int sensor_s_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    int i, error_cnt=0, error_idx=-1;

    for (i=0; i<ext_ctrl->count; i++) {
        if (sensor_s_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
	#if CONFIG_SENSOR_Focus
	struct soc_camera_device *icd = client->dev.platform_data;
	struct v4l2_mbus_framefmt mf;
	#endif
    
	if (enable == 1) {
		sensor->info_priv.enable = 1;
		#if CONFIG_SENSOR_Focus
		mf.width	= icd->user_width;
    	mf.height	= icd->user_height;
    	mf.code	= sensor->info_priv.fmt.code;
    	mf.colorspace	= sensor->info_priv.fmt.colorspace;
    	mf.field	= V4L2_FIELD_NONE;
		/* If auto focus firmware haven't download success, must download firmware again when in video or preview stream on */
		if (sensor_fmt_capturechk(sd, &mf) == false) {
			if ((sensor->info_priv.affm_reinit == 1) || ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK)==0)) {
                sensor_af_workqueue_set(icd, WqCmd_af_init, 0, false,NULL);
				sensor->info_priv.affm_reinit = 0;
			}
		}
		#endif
	} else if (enable == 0) {
	    sensor->info_priv.enable = 0;
		#if CONFIG_SENSOR_Focus	
        flush_workqueue(sensor->sensor_wq);
		#endif
	}
	return 0;
}

/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int sensor_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{
    char value;
    int ret,pid = 0;
    struct sensor *sensor = to_sensor(client);

    /* We must have a parent by now. And it cannot be a wrong one.
     * So this entire test is completely redundant. */
    if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;
#if 1 //derek
	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_video_probe_err;
	}
#endif	
    /* soft reset */
    /*
    ret = sensor_write(client, 0x3008, 0x80);
    if (ret != 0) {
        SENSOR_TR("soft reset %s failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_video_probe_err;
    }
    mdelay(5);          //delay 5 microseconds
*/
    printk("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    mdelay(5000); //jzw modify

    
    /* check if it is an sensor sensor */
    ret = sensor_read(client, 0x0000, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id high byte failed\n");
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }
    printk("jzw:%s:0x%02x\n",__FUNCTION__,value);
    pid |= (value << 8);

    ret = sensor_read(client, 0x0001, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id low byte failed\n");
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }
     printk("jzw:%s:0x%02x\n",__FUNCTION__,value);
    pid |= (value & 0xff);
    SENSOR_DG("\n %s  pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
    if (pid == SENSOR_ID) {
        sensor->model = SENSOR_V4L2_IDENT;
    } else {
        SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    return 0;

sensor_video_probe_err:
    return ret;
}
static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
    int ret = 0,i;

	SENSOR_DG("\n%s..%s..cmd:%x \n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
	switch (cmd)
	{
		case RK29_CAM_SUBDEV_DEACTIVATE:
		{
			sensor_deactivate(client);
			break;
		}
		case RK29_CAM_SUBDEV_IOREQUEST:
		{
            sensor->sensor_io_request = (struct rk29camera_platform_data*)arg;           
            if (sensor->sensor_io_request != NULL) { 
                sensor->sensor_gpio_res = NULL;
                for (i=0; i<RK29_CAM_SUPPORT_NUMS;i++) {
                    if (sensor->sensor_io_request->gpio_res[i].dev_name && 
                        (strcmp(sensor->sensor_io_request->gpio_res[i].dev_name, dev_name(icd->pdev)) == 0)) {
                        sensor->sensor_gpio_res = (struct rk29camera_gpio_res*)&sensor->sensor_io_request->gpio_res[i];
                    }
                }
                if (sensor->sensor_gpio_res == NULL) {
                    SENSOR_TR("%s %s obtain gpio resource failed when RK29_CAM_SUBDEV_IOREQUEST \n",SENSOR_NAME_STRING(),__FUNCTION__);
                    ret = -EINVAL;
                    goto sensor_ioctl_end;
                }
            } else {
                SENSOR_TR("%s %s RK29_CAM_SUBDEV_IOREQUEST fail\n",SENSOR_NAME_STRING(),__FUNCTION__);
                ret = -EINVAL;
                goto sensor_ioctl_end;
            }
            /* ddl@rock-chips.com : if gpio_flash havn't been set in board-xxx.c, sensor driver must notify is not support flash control 
               for this project */
            #if CONFIG_SENSOR_Flash	
        	if (sensor->sensor_gpio_res) {
                printk("flash io:%d\n",sensor->sensor_gpio_res->gpio_flash);
                if (sensor->sensor_gpio_res->gpio_flash == INVALID_GPIO) {
                    for (i = 0; i < icd->ops->num_controls; i++) {
                		if (V4L2_CID_FLASH == icd->ops->controls[i].id) {
                			//memset((char*)&icd->ops->controls[i],0x00,sizeof(struct v4l2_queryctrl));  
                              sensor_controls[i].id=0xffff;         			
                		}
                    }
                    sensor->info_priv.flash = 0xff;
                    SENSOR_DG("%s flash gpio is invalidate!\n",SENSOR_NAME_STRING());
                }else{ //two cameras are the same,need to deal diffrently ,zyc
                    for (i = 0; i < icd->ops->num_controls; i++) {
                           if(0xffff == icd->ops->controls[i].id){
                              sensor_controls[i].id=V4L2_CID_FLASH;
                           }               
                    }
                }
        	}
            #endif
			break;
		}
		default:
		{
			SENSOR_TR("%s %s cmd(0x%x) is unknown !\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
			break;
		}
	}

sensor_ioctl_end:
	return ret;

}
static int sensor_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(sensor_colour_fmts))
		return -EINVAL;

	*code = sensor_colour_fmts[index].code;
	return 0;
}
static struct v4l2_subdev_core_ops sensor_subdev_core_ops = {
	.init		= sensor_init,
	.g_ctrl		= sensor_g_control,
	.s_ctrl		= sensor_s_control,
	.g_ext_ctrls          = sensor_g_ext_controls,
	.s_ext_ctrls          = sensor_s_ext_controls,
	.g_chip_ident	= sensor_g_chip_ident,
	.ioctl = sensor_ioctl,
};

static struct v4l2_subdev_video_ops sensor_subdev_video_ops = {
	.s_mbus_fmt	= sensor_s_fmt,
	.g_mbus_fmt	= sensor_g_fmt,
	.try_mbus_fmt	= sensor_try_fmt,
	.enum_mbus_fmt	= sensor_enum_fmt,
	.s_stream   = sensor_s_stream,
};

static struct v4l2_subdev_ops sensor_subdev_ops = {
	.core	= &sensor_subdev_core_ops,
	.video = &sensor_subdev_video_ops,
};

static int sensor_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
    struct sensor *sensor;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
    struct soc_camera_link *icl;
    int ret;

    SENSOR_DG("\n%s..%s..%d..\n",__FUNCTION__,__FILE__,__LINE__);
    if (!icd) {
        dev_err(&client->dev, "%s: missing soc-camera data!\n",SENSOR_NAME_STRING());
        return -EINVAL;
    }

    icl = to_soc_camera_link(icd);
    if (!icl) {
        dev_err(&client->dev, "%s driver needs platform data\n", SENSOR_NAME_STRING());
        return -EINVAL;
    }

    if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
        dev_warn(&adapter->dev,
        	 "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
        return -EIO;
    }

    sensor = kzalloc(sizeof(struct sensor), GFP_KERNEL);
    if (!sensor)
        return -ENOMEM;

    v4l2_i2c_subdev_init(&sensor->subdev, client, &sensor_subdev_ops);

    /* Second stage probe - when a capture adapter is there */
    icd->ops		= &sensor_ops;
    sensor->info_priv.fmt = sensor_colour_fmts[0];
	#if CONFIG_SENSOR_I2C_NOSCHED
	atomic_set(&sensor->tasklock_cnt,0);
	#endif

    ret = sensor_video_probe(icd, client);
    if (ret < 0) {
        icd->ops = NULL;
        i2c_set_clientdata(client, NULL);
        kfree(sensor);
		sensor = NULL;
    } else {
		#if CONFIG_SENSOR_Focus
		sensor->sensor_wq = create_workqueue(SENSOR_NAME_STRING(_af_workqueue));
		if (sensor->sensor_wq == NULL)
			SENSOR_TR("%s create fail!", SENSOR_NAME_STRING(_af_workqueue));
		mutex_init(&sensor->wq_lock);
		#endif
    }
	hrtimer_init(&(flash_off_timer.timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    SENSOR_DG("\n%s..%s..%d  ret = %x \n",__FUNCTION__,__FILE__,__LINE__,ret);
    return ret;
}

static int sensor_remove(struct i2c_client *client)
{
    struct sensor *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;

	#if CONFIG_SENSOR_Focus
	if (sensor->sensor_wq) {
		destroy_workqueue(sensor->sensor_wq);
		sensor->sensor_wq = NULL;
	}
	#endif

    icd->ops = NULL;
    i2c_set_clientdata(client, NULL);
    client->driver = NULL;
    kfree(sensor);
	sensor = NULL;
    return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{SENSOR_NAME_STRING(), 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_i2c_driver = {
	.driver = {
		.name = SENSOR_NAME_STRING(),
	},
	.probe		= sensor_probe,
	.remove		= sensor_remove,
	.id_table	= sensor_id,
};

static int __init sensor_mod_init(void)
{
   // if (!tcsi_get_value(WESI_CAMERA_HM5065)) //add by caixx
   //     return 0;

    SENSOR_DG("\n%s..%s.. \n",__FUNCTION__,SENSOR_NAME_STRING());
    return i2c_add_driver(&sensor_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
    //if (!tcsi_get_value(WESI_CAMERA_HM5065)) //add by caixx
   //     return;

    i2c_del_driver(&sensor_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION(SENSOR_NAME_STRING(Camera sensor driver));
MODULE_AUTHOR("ddl <kernel@rock-chips>");
MODULE_LICENSE("GPL");

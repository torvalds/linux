/*
 * Driver for mt9d112 CMOS Image Sensor from Aptina(micron)
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
#include <mach/rk_camera.h>
#include <linux/vmalloc.h>
#include "mt9d112.h"

static int debug;
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
#define SENSOR_NAME RK_CAM_SENSOR_MT9D112
#define SENSOR_V4L2_IDENT V4L2_IDENT_MT9D112
#define SENSOR_ID 0x1580
#define SENSOR_ID_REG 0x3000
#define SENSOR_RESET_REG SEQUENCE_END
#define SENSOR_RESET_VAL 0x00
#define SENSOR_MIN_WIDTH    320
#define SENSOR_MIN_HEIGHT   240
#define SENSOR_MAX_WIDTH    1600
#define SENSOR_MAX_HEIGHT   1200
#define SENSOR_INIT_WIDTH	sensor_init_width			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  sensor_init_height
#define SENSOR_INIT_WINSEQADR  sensor_init_winseq_p
#define SENSOR_INIT_PIXFMT sensor_init_pixelcode
#define SENSOR_BUS_PARAM  sensor_init_busparam

#define CONFIG_SENSOR_WhiteBalance	1
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast      0
#define CONFIG_SENSOR_Saturation    0
#define CONFIG_SENSOR_Effect        1
#define CONFIG_SENSOR_Scene         1
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Exposure      0
#define CONFIG_SENSOR_Flash         0
#define CONFIG_SENSOR_Mirror        0
#define CONFIG_SENSOR_Flip          0
#define CONFIG_SENSOR_Focus         0


#define CONFIG_SENSOR_I2C_SPEED     250000       /* Hz */
/* Sensor write register continues by preempt_disable/preempt_enable for current process not be scheduled */
#define CONFIG_SENSOR_I2C_NOSCHED   0
#define CONFIG_SENSOR_I2C_RDWRCHK   0

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
#define SENSOR_AF_MODE_INFINITY    0
#define SENSOR_AF_MODE_MACRO       1
#define SENSOR_AF_MODE_FIXED       2
#define SENSOR_AF_MODE_AUTO        3
#define SENSOR_AF_MODE_CONTINUOUS  4
#define SENSOR_AF_MODE_CLOSE       5
#endif
#define SENSOR_CCM_ONLY
static s32 sensor_init_width = 0;
static s32 sensor_init_height = 0;
static unsigned long sensor_init_busparam = (SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING|SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_LOW |SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8  |SOCAM_MCLK_24MHZ);
static enum v4l2_mbus_pixelcode sensor_init_pixelcode = V4L2_MBUS_FMT_YUYV8_2X8;
static struct reginfo* sensor_init_data_p = NULL;
static struct reginfo* sensor_init_winseq_p = NULL;
static struct reginfo* sensor_init_winseq_board = NULL;
/* init 640X480 VGA */
static struct reginfo sensor_init_data[] =
{
	{0x301A, 0x0ACC}, 	// RESET_REGISTER
	{0x3202, 0x0008}, 	// STANDBY_CONTROL
	{SEQUENCE_WAIT_MS, 10},
	{0x341E, 0x8F09}, 	// PLL_CLK_IN_CONTROL
	{0x341C, 0x0218}, 	// PLL_DIVIDERS1
	{SEQUENCE_WAIT_MS, 1},
	{0x341E, 0x8F09}, 	// PLL_CLK_IN_CONTROL
	{0x341E, 0x8F08}, 	// PLL_CLK_IN_CONTROL
	{0x3044, 0x0540}, 	// DARK_CONTROL
	{0x3216, 0x02CF}, 	// INTERNAL_CLOCK_CONTROL
	{0x321C, 0x0402}, 	// OF_CONTROL_STATUS
	{0x3212, 0x0001}, 	// FACTORY_BYPASS
	{0x341E, 0x8F09}, 	// PLL_CLK_IN_CONTROL
	{0x341C, 0x0120}, 	// PLL_DIVIDERS1
	{0x341E, 0x8F09}, 	// PLL_CLK_IN_CONTROL
	{0x341E, 0x8F08}, 	// PLL_CLK_IN_CONTROL
	{0x3044, 0x0540}, 	// DARK_CONTROL
	{0x3216, 0x02CF}, 	// INTERNAL_CLOCK_CONTROL
	{0x321C, 0x0402}, 	// OF_CONTROL_STATUS
	{0x3212, 0x0001}, 	// FACTORY_BYPASS//供品干扰程序利用之前的07005
#ifdef SENSOR_CCM_ONLY
	{0x338C, 0x2703},
	{0x3390, 0x0320},
	{0x338C, 0x2705},
	{0x3390, 0x0258},
	{0x338C, 0x2707},
	{0x3390, 0x0640},
	{0x338C, 0x2709},
	{0x3390, 0x04B0},
	{0x338C, 0x270D},
	{0x3390, 0x0000},
	{0x338C, 0x270F},
	{0x3390, 0x0000},
	{0x338C, 0x2711},
	{0x3390, 0x4BD},
	{0x338C, 0x2713},
	{0x3390, 0x64D},
	{0x3390, 0x411},
#else
	{0x338C, 0x2703}, 	// MCU_ADDRESS
	{0x3390, 0x0140}, 	// MCU_DATA_0
	{0x338C, 0x2705}, 	// MCU_ADDRESS
	{0x3390, 0x00F0}, 	// MCU_DATA_0
	{0x338C, 0x2707}, 	// MCU_ADDRESS
	{0x3390, 0x0640}, 	// MCU_DATA_0
	{0x338C, 0x2709}, 	// MCU_ADDRESS
	{0x3390, 0x04B0}, 	// MCU_DATA_0
	{0x338C, 0x270D}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x270F}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2711}, 	// MCU_ADDRESS
	{0x3390, 0x04BD}, 	// MCU_DATA_0
	{0x338C, 0x2713}, 	// MCU_ADDRESS
	{0x3390, 0x064D}, 	// MCU_DATA_0
	{0x338C, 0x2715}, 	// MCU_ADDRESS
	{0x3390, 0x0022}, 	// MCU_DATA_0
#endif
	{0x338C, 0x2717}, 	// MCU_ADDRESS
	{0x3390, 0x2111}, 	// MCU_DATA_0
	{0x338C, 0x2719}, 	// MCU_ADDRESS
	{0x3390, 0x046C}, 	// MCU_DATA_0
	{0x338C, 0x271B}, 	// MCU_ADDRESS
	{0x3390, 0x024F}, 	// MCU_DATA_0
	{0x338C, 0x271D}, 	// MCU_ADDRESS
	{0x3390, 0x0102}, 	// MCU_DATA_0
	{0x338C, 0x271F}, 	// MCU_ADDRESS
	{0x3390, 0x0279}, 	// MCU_DATA_0
	{0x338C, 0x2721}, 	// MCU_ADDRESS
	{0x3390, 0x0155}, 	// MCU_DATA_0
	{0x338C, 0x2723}, 	// MCU_ADDRESS
	{0x3390, 0x0293}, 	// MCU_DATA_0
	{0x338C, 0x2725}, 	// MCU_ADDRESS
	{0x3390, 0x0824}, 	// MCU_DATA_0
	{0x338C, 0x2727}, 	// MCU_ADDRESS
	{0x3390, 0x2020}, 	// MCU_DATA_0
	{0x338C, 0x2729}, 	// MCU_ADDRESS
	{0x3390, 0x2020}, 	// MCU_DATA_0
	{0x338C, 0x272B}, 	// MCU_ADDRESS
	{0x3390, 0x1020}, 	// MCU_DATA_0
	{0x338C, 0x272D}, 	// MCU_ADDRESS
	{0x3390, 0x2007}, 	// MCU_DATA_0
	{0x338C, 0x272F}, 	// MCU_ADDRESS
	{0x3390, 0x0004}, 	// MCU_DATA_0
	{0x338C, 0x2731}, 	// MCU_ADDRESS
	{0x3390, 0x0004}, 	// MCU_DATA_0
	{0x338C, 0x2733}, 	// MCU_ADDRESS
	{0x3390, 0x04BB}, 	// MCU_DATA_0
	{0x338C, 0x2735}, 	// MCU_ADDRESS
	{0x3390, 0x064B}, 	// MCU_DATA_0
	{0x338C, 0x2737}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2739}, 	// MCU_ADDRESS
	{0x3390, 0x2111}, 	// MCU_DATA_0
	{0x338C, 0x273B}, 	// MCU_ADDRESS
	{0x3390, 0x0024}, 	// MCU_DATA_0
	{0x338C, 0x273D}, 	// MCU_ADDRESS
	{0x3390, 0x0120}, 	// MCU_DATA_0
	{0x338C, 0x273F}, 	// MCU_ADDRESS
	{0x3390, 0x00A4}, 	// MCU_DATA_0
	{0x338C, 0x2741}, 	// MCU_ADDRESS
	{0x3390, 0x0169}, 	// MCU_DATA_0
	{0x338C, 0x2743}, 	// MCU_ADDRESS
	{0x3390, 0x00A4}, 	// MCU_DATA_0
	{0x338C, 0x2745}, 	// MCU_ADDRESS
	{0x3390, 0x04ED}, 	// MCU_DATA_0
	{0x338C, 0x2747}, 	// MCU_ADDRESS
	{0x3390, 0x0824}, 	// MCU_DATA_0
	{0x338C, 0x2751}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2753}, 	// MCU_ADDRESS
	{0x3390, 0x0320}, 	// MCU_DATA_0
	{0x338C, 0x2755}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2757}, 	// MCU_ADDRESS
	{0x3390, 0x0258}, 	// MCU_DATA_0
	{0x338C, 0x275F}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2761}, 	// MCU_ADDRESS
	{0x3390, 0x0640}, 	// MCU_DATA_0
	{0x338C, 0x2763}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2765}, 	// MCU_ADDRESS
	{0x3390, 0x04B0}, 	// MCU_DATA_0
	{0x338C, 0x222E}, 	// MCU_ADDRESS
	{0x3390, 0x0060}, 	// MCU_DATA_0
	{0x338C, 0xA408}, 	// MCU_ADDRESS
	{0x3390, 0x0017}, 	// MCU_DATA_0
	{0x338C, 0xA409}, 	// MCU_ADDRESS
	{0x3390, 0x001A}, 	// MCU_DATA_0
	{0x338C, 0xA40A}, 	// MCU_ADDRESS
	{0x3390, 0x001B}, 	// MCU_DATA_0
	{0x338C, 0xA40B}, 	// MCU_ADDRESS
	{0x3390, 0x001E}, 	// MCU_DATA_0
	{0x338C, 0x2411}, 	// MCU_ADDRESS
	{0x3390, 0x0060}, 	// MCU_DATA_0
	{0x338C, 0x2413}, 	// MCU_ADDRESS
	{0x3390, 0x0073}, 	// MCU_DATA_0
	{0x338C, 0x2415}, 	// MCU_ADDRESS
	{0x3390, 0x0060}, 	// MCU_DATA_0
	{0x338C, 0x2417}, 	// MCU_ADDRESS
	{0x3390, 0x0073}, 	// MCU_DATA_0
	{0x338C, 0xA40D}, 	// MCU_ADDRESS
	{0x3390, 0x0002}, 	// MCU_DATA_0
	{0x338C, 0xA410}, 	// MCU_ADDRESS
	{0x3390, 0x0001}, 	// MCU_DATA_0
	{0x338C, 0xA103}, 	// MCU_ADDRESS
	{0x3390, 0x0006}, 	// MCU_DATA_0
	{0x338C, 0xA103}, 	// MCU_ADDRESS
	{0x3390, 0x0005}, 	// MCU_DATA_0
	{0x338C, 0x2703}, 	// MCU_ADDRESS
	{0x3390, 0x0140}, 	// MCU_DATA_0
	{0x338C, 0x2705}, 	// MCU_ADDRESS
	{0x3390, 0x00F0}, 	// MCU_DATA_0
	{0x338C, 0x2707}, 	// MCU_ADDRESS
	{0x3390, 0x0640}, 	// MCU_DATA_0
	{0x338C, 0x2709}, 	// MCU_ADDRESS
	{0x3390, 0x04B0}, 	// MCU_DATA_0
	{0x338C, 0x270D}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x270F}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2711}, 	// MCU_ADDRESS
	{0x3390, 0x04BD}, 	// MCU_DATA_0
	{0x338C, 0x2713}, 	// MCU_ADDRESS
	{0x3390, 0x064D}, 	// MCU_DATA_0
	{0x338C, 0x2715}, 	// MCU_ADDRESS
	{0x3390, 0x0022}, 	// MCU_DATA_0
	{0x338C, 0x2717}, 	// MCU_ADDRESS
	{0x3390, 0x2111}, 	// MCU_DATA_0
	{0x338C, 0x2719}, 	// MCU_ADDRESS
	{0x3390, 0x046C}, 	// MCU_DATA_0
	{0x338C, 0x271B}, 	// MCU_ADDRESS
	{0x3390, 0x024F}, 	// MCU_DATA_0
	{0x338C, 0x271D}, 	// MCU_ADDRESS
	{0x3390, 0x0102}, 	// MCU_DATA_0
	{0x338C, 0x271F}, 	// MCU_ADDRESS
	{0x3390, 0x0279}, 	// MCU_DATA_0
	{0x338C, 0x2721}, 	// MCU_ADDRESS
	{0x3390, 0x0155}, 	// MCU_DATA_0
	{0x338C, 0x2723}, 	// MCU_ADDRESS
	{0x3390, 0x0293}, 	// MCU_DATA_0
	{0x338C, 0x2725}, 	// MCU_ADDRESS
	{0x3390, 0x0824}, 	// MCU_DATA_0
	{0x338C, 0x2727}, 	// MCU_ADDRESS
	{0x3390, 0x2020}, 	// MCU_DATA_0
	{0x338C, 0x2729}, 	// MCU_ADDRESS
	{0x3390, 0x2020}, 	// MCU_DATA_0
	{0x338C, 0x272B}, 	// MCU_ADDRESS
	{0x3390, 0x1020}, 	// MCU_DATA_0
	{0x338C, 0x272D}, 	// MCU_ADDRESS
	{0x3390, 0x2007}, 	// MCU_DATA_0
	{0x338C, 0x272F}, 	// MCU_ADDRESS
	{0x3390, 0x0004}, 	// MCU_DATA_0
	{0x338C, 0x2731}, 	// MCU_ADDRESS
	{0x3390, 0x0004}, 	// MCU_DATA_0
	{0x338C, 0x2733}, 	// MCU_ADDRESS
	{0x3390, 0x04BB}, 	// MCU_DATA_0
	{0x338C, 0x2735}, 	// MCU_ADDRESS
	{0x3390, 0x064B}, 	// MCU_DATA_0
	{0x338C, 0x2737}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2739}, 	// MCU_ADDRESS
	{0x3390, 0x2111}, 	// MCU_DATA_0
	{0x338C, 0x273B}, 	// MCU_ADDRESS
	{0x3390, 0x0024}, 	// MCU_DATA_0
	{0x338C, 0x273D}, 	// MCU_ADDRESS
	{0x3390, 0x0120}, 	// MCU_DATA_0
	{0x338C, 0x273F}, 	// MCU_ADDRESS
	{0x3390, 0x00A4}, 	// MCU_DATA_0
	{0x338C, 0x2741}, 	// MCU_ADDRESS
	{0x3390, 0x0169}, 	// MCU_DATA_0
	{0x338C, 0x2743}, 	// MCU_ADDRESS
	{0x3390, 0x00A4}, 	// MCU_DATA_0
	{0x338C, 0x2745}, 	// MCU_ADDRESS
	{0x3390, 0x04ED}, 	// MCU_DATA_0
	{0x338C, 0x2747}, 	// MCU_ADDRESS
	{0x3390, 0x0824}, 	// MCU_DATA_0
	{0x338C, 0x2751}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2753}, 	// MCU_ADDRESS
	{0x3390, 0x0320}, 	// MCU_DATA_0
	{0x338C, 0x2755}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2757}, 	// MCU_ADDRESS
	{0x3390, 0x0258}, 	// MCU_DATA_0
	{0x338C, 0x275F}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2761}, 	// MCU_ADDRESS
	{0x3390, 0x0640}, 	// MCU_DATA_0
	{0x338C, 0x2763}, 	// MCU_ADDRESS
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0x2765}, 	// MCU_ADDRESS
	{0x3390, 0x04B0}, 	// MCU_DATA_0
	{0x338C, 0x222E}, 	// MCU_ADDRESS
	{0x3390, 0x0060}, 	// MCU_DATA_0
	{0x338C, 0xA408}, 	// MCU_ADDRESS
	{0x3390, 0x0017}, 	// MCU_DATA_0
	{0x338C, 0xA409}, 	// MCU_ADDRESS
	{0x3390, 0x001A}, 	// MCU_DATA_0
	{0x338C, 0xA40A}, 	// MCU_ADDRESS
	{0x3390, 0x001B}, 	// MCU_DATA_0
	{0x338C, 0xA40B}, 	// MCU_ADDRESS
	{0x3390, 0x001E}, 	// MCU_DATA_0
	{0x338C, 0x2411}, 	// MCU_ADDRESS
	{0x3390, 0x0060}, 	// MCU_DATA_0
	{0x338C, 0x2413}, 	// MCU_ADDRESS
	{0x3390, 0x0073}, 	// MCU_DATA_0
	{0x338C, 0x2415}, 	// MCU_ADDRESS
	{0x3390, 0x0060}, 	// MCU_DATA_0
	{0x338C, 0x2417}, 	// MCU_ADDRESS
	{0x3390, 0x0073}, 	// MCU_DATA_0
	{0x338C, 0xA40D}, 	// MCU_ADDRESS
	{0x3390, 0x0002}, 	// MCU_DATA_0
	{0x338C, 0xA410}, 	// MCU_ADDRESS
	{0x3390, 0x0001}, 	// MCU_DATA_0
	{0x338C, 0xA103}, 	// MCU_ADDRESS
	{0x3390, 0x0006}, 	// MCU_DATA_0
	{SEQUENCE_WAIT_MS, 100},
	{0x338C, 0xA103}, 	// MCU_ADDRESS
	{0x3390, 0x0005}, 	// MCU_DATA_0
	{SEQUENCE_WAIT_MS, 100},


	//[Lens Correction 01/04/07 20:07:28]
	{0x34CE, 0x01A0}, //LENS_CORRECTION_CONTROL
	{0x34D0, 0x6532}, //ZONE_BOUNDS_X1_X2
	{0x34D2, 0x3297}, //ZONE_BOUNDS_X0_X3
	{0x34D4, 0x9664}, //ZONE_BOUNDS_X4_X5
	{0x34D6, 0x4B25}, //ZONE_BOUNDS_Y1_Y2
	{0x34D8, 0x2670}, //ZONE_BOUNDS_Y0_Y3
	{0x34DA, 0x724C}, //ZONE_BOUNDS_Y4_Y5
	{0x34DC, 0xFF01}, //CENTER_OFFSET
	{0x34DE, 0x011F}, //FX_RED
	{0x34E6, 0x00B3}, //FY_RED
	{0x34EE, 0x0D8F}, //DF_DX_RED
	{0x34F6, 0x0D6E}, //DF_DY_RED
	{0x3500, 0xF51C}, //SECOND_DERIV_ZONE_0_RED
	{0x3508, 0xFEF8}, //SECOND_DERIV_ZONE_1_RED
	{0x3510, 0x212D}, //SECOND_DERIV_ZONE_2_RED
	{0x3518, 0x212B}, //SECOND_DERIV_ZONE_3_RED
	{0x3520, 0x1F2A}, //SECOND_DERIV_ZONE_4_RED
	{0x3528, 0x2934}, //SECOND_DERIV_ZONE_5_RED
	{0x3530, 0x17DB}, //SECOND_DERIV_ZONE_6_RED
	{0x3538, 0xE7D6}, //SECOND_DERIV_ZONE_7_RED
	{0x354C, 0x07C6}, //K_FACTOR_IN_K_FX_FY_R_TL
	{0x3544, 0x07FF}, //K_FACTOR_IN_K_FX_FY_R_TR
	{0x355C, 0x051C}, //K_FACTOR_IN_K_FX_FY_R_BL
	{0x3554, 0x07FF}, //K_FACTOR_IN_K_FX_FY_R_BR
	{0x34E0, 0x0137}, //FX_GREEN
	{0x34E8, 0x00A8}, //FY_GREEN
	{0x34F0, 0x0E3E}, //DF_DX_GREEN
	{0x34F8, 0x0DCB}, //DF_DY_GREEN
	{0x3502, 0xF20A}, //SECOND_DERIV_ZONE_0_GREEN
	{0x350A, 0xFBE1}, //SECOND_DERIV_ZONE_1_GREEN
	{0x3512, 0x1C26}, //SECOND_DERIV_ZONE_2_GREEN
	{0x351A, 0x232B}, //SECOND_DERIV_ZONE_3_GREEN
	{0x3522, 0x312E}, //SECOND_DERIV_ZONE_4_GREEN
	{0x352A, 0x2121}, //SECOND_DERIV_ZONE_5_GREEN
	{0x3532, 0xF106}, //SECOND_DERIV_ZONE_6_GREEN
	{0x353A, 0x0C0D}, //SECOND_DERIV_ZONE_7_GREEN
	{0x354E, 0x07FF}, //K_FACTOR_IN_K_FX_FY_G1_TL
	{0x3546, 0x0638}, //K_FACTOR_IN_K_FX_FY_G1_TR
	{0x355E, 0x07FF}, //K_FACTOR_IN_K_FX_FY_G1_BL
	{0x3556, 0x0155}, //K_FACTOR_IN_K_FX_FY_G1_BR
	{0x34E4, 0x0107}, //FX_BLUE
	{0x34EC, 0x0079}, //FY_BLUE
	{0x34F4, 0x0E19}, //DF_DX_BLUE
	{0x34FC, 0x0D35}, //DF_DY_BLUE
	{0x3506, 0x111C}, //SECOND_DERIV_ZONE_0_BLUE
	{0x350E, 0x02E6}, //SECOND_DERIV_ZONE_1_BLUE
	{0x3516, 0x2521}, //SECOND_DERIV_ZONE_2_BLUE
	{0x351E, 0x2620}, //SECOND_DERIV_ZONE_3_BLUE
	{0x3526, 0x1A25}, //SECOND_DERIV_ZONE_4_BLUE
	{0x352E, 0x0B1D}, //SECOND_DERIV_ZONE_5_BLUE
	{0x3536, 0xFD03}, //SECOND_DERIV_ZONE_6_BLUE
	{0x353E, 0xB315}, //SECOND_DERIV_ZONE_7_BLUE
	{0x3552, 0x06D0}, //K_FACTOR_IN_K_FX_FY_B_TL
	{0x354A, 0x03FF}, //K_FACTOR_IN_K_FX_FY_B_TR
	{0x3562, 0x07FF}, //K_FACTOR_IN_K_FX_FY_B_BL
	{0x355A, 0x057B}, //K_FACTOR_IN_K_FX_FY_B_BR
	{0x34E2, 0x011E}, //FX_GREEN2
	{0x34EA, 0x008A}, //FY_GREEN2
	{0x34F2, 0x0D6E}, //DF_DX_GREEN2
	{0x34FA, 0x0D7F}, //DF_DY_GREEN2
	{0x3504, 0xF822}, //SECOND_DERIV_ZONE_0_GREEN2
	{0x350C, 0x0DFC}, //SECOND_DERIV_ZONE_1_GREEN2
	{0x3514, 0x1F25}, //SECOND_DERIV_ZONE_2_GREEN2
	{0x351C, 0x3032}, //SECOND_DERIV_ZONE_3_GREEN2
	{0x3524, 0x2628}, //SECOND_DERIV_ZONE_4_GREEN2
	{0x352C, 0x1523}, //SECOND_DERIV_ZONE_5_GREEN2
	{0x3534, 0xFADF}, //SECOND_DERIV_ZONE_6_GREEN2
	{0x353C, 0xDEF7}, //SECOND_DERIV_ZONE_7_GREEN2
	{0x3550, 0x0109}, //K_FACTOR_IN_K_FX_FY_G2_TL
	{0x3548, 0x0638}, //K_FACTOR_IN_K_FX_FY_G2_TR
	{0x3560, 0x0638}, //K_FACTOR_IN_K_FX_FY_G2_BL
	{0x3558, 0x07FF}, //K_FACTOR_IN_K_FX_FY_G2_BR
	{0x3540, 0x0000}, //X2_FACTORS
	{0x3542, 0x0000}, //GLOBAL_OFFSET_FXY_FUNCTION
	{0x3210, 0x01FC}, 	// COLOR_PIPELINE_CONTROL
	//CCM
	//CCM
	{0x338C, 0xA364}, 	// MCU_ADDRESS [AWB_KR_L]
	{0x3390, 0x0080}, 	// MCU_DATA_0
	{0x338C, 0xA364}, 	// MCU_ADDRESS [AWB_KR_L]
	{0x3390, 0x0080}, 	// MCU_DATA_0
	{0x338C, 0xA365}, 	// MCU_ADDRESS [AWB_KG_L]
	{0x3390, 0x008C}, 	// MCU_DATA_0
	{0x338C, 0xA365}, 	// MCU_ADDRESS [AWB_KG_L]
	{0x3390, 0x008C}, 	// MCU_DATA_0
	{0x338C, 0xA366}, 	// MCU_ADDRESS [AWB_KB_L]
	{0x3390, 0x0082}, 	// MCU_DATA_0
	{0x338C, 0xA366}, 	// MCU_ADDRESS [AWB_KB_L]
	{0x3390, 0x0082}, 	// MCU_DATA_0
	{0x338C, 0x2306}, 	// MCU_ADDRESS [AWB_CCM_L_0]
	{0x3390, 0x0619}, 	// MCU_DATA_0
	{0x338C, 0x2308}, 	// MCU_ADDRESS [AWB_CCM_L_1]
	{0x3390, 0xFC80}, 	// MCU_DATA_0
	{0x338C, 0x230A}, 	// MCU_ADDRESS [AWB_CCM_L_2]
	{0x3390, 0xFEFB}, 	// MCU_DATA_0
	{0x338C, 0x230C}, 	// MCU_ADDRESS [AWB_CCM_L_3]
	{0x3390, 0xFEEE}, 	// MCU_DATA_0
	{0x338C, 0x230E}, 	// MCU_ADDRESS [AWB_CCM_L_4]
	{0x3390, 0x0571}, 	// MCU_DATA_0
	{0x338C, 0x2310}, 	// MCU_ADDRESS [AWB_CCM_L_5]
	{0x3390, 0xFE26}, 	// MCU_DATA_0
	{0x338C, 0x2312}, 	// MCU_ADDRESS [AWB_CCM_L_6]
	{0x3390, 0xFF0C}, 	// MCU_DATA_0
	{0x338C, 0x2314}, 	// MCU_ADDRESS [AWB_CCM_L_7]
	{0x3390, 0xFE48}, 	// MCU_DATA_0
	{0x338C, 0x2316}, 	// MCU_ADDRESS [AWB_CCM_L_8]
	{0x3390, 0x04A2}, 	// MCU_DATA_0
	{0x338C, 0x2318}, 	// MCU_ADDRESS [AWB_CCM_L_9]
	{0x3390, 0x0024}, 	// MCU_DATA_0
	{0x338C, 0x231A}, 	// MCU_ADDRESS [AWB_CCM_L_10]
	{0x3390, 0x003F}, 	// MCU_DATA_0
	{0x338C, 0x231C}, 	// MCU_ADDRESS [AWB_CCM_RL_0]
	{0x3390, 0xFDA7}, 	// MCU_DATA_0
	{0x338C, 0x231E}, 	// MCU_ADDRESS [AWB_CCM_RL_1]
	{0x3390, 0x0158}, 	// MCU_DATA_0
	{0x338C, 0x2320}, 	// MCU_ADDRESS [AWB_CCM_RL_2]
	{0x3390, 0x00EE}, 	// MCU_DATA_0
	{0x338C, 0x2322}, 	// MCU_ADDRESS [AWB_CCM_RL_3]
	{0x3390, 0x00D3}, 	// MCU_DATA_0
	{0x338C, 0x2324}, 	// MCU_ADDRESS [AWB_CCM_RL_4]
	{0x3390, 0xFC74}, 	// MCU_DATA_0
	{0x338C, 0x2326}, 	// MCU_ADDRESS [AWB_CCM_RL_5]
	{0x3390, 0x01A9}, 	// MCU_DATA_0
	{0x338C, 0x2328}, 	// MCU_ADDRESS [AWB_CCM_RL_6]
	{0x3390, 0x014B}, 	// MCU_DATA_0
	{0x338C, 0x232A}, 	// MCU_ADDRESS [AWB_CCM_RL_7]
	{0x3390, 0xFE9D}, 	// MCU_DATA_0
	{0x338C, 0x232C}, 	// MCU_ADDRESS [AWB_CCM_RL_8]
	{0x3390, 0xFF69}, 	// MCU_DATA_0
	{0x338C, 0x232E}, 	// MCU_ADDRESS [AWB_CCM_RL_9]
	{0x3390, 0x0018}, 	// MCU_DATA_0
	{0x338C, 0x2330}, 	// MCU_ADDRESS [AWB_CCM_RL_10]
	{0x3390, 0xFFEC}, 	// MCU_DATA_0
	{0x338C, 0xA348}, 	// MCU_ADDRESS [AWB_GAIN_BUFFER_SPEED]
	{0x3390, 0x0008}, 	// MCU_DATA_0
	{0x338C, 0xA349}, 	// MCU_ADDRESS [AWB_JUMP_DIVISOR]
	{0x3390, 0x0002}, 	// MCU_DATA_0
	{0x338C, 0xA34A}, 	// MCU_ADDRESS [AWB_GAIN_MIN]
	{0x3390, 0x0059}, 	// MCU_DATA_0
	{0x338C, 0xA34B}, 	// MCU_ADDRESS [AWB_GAIN_MAX]
	{0x3390, 0x00A6}, 	// MCU_DATA_0
	{0x338C, 0xA34F}, 	// MCU_ADDRESS [AWB_CCM_POSITION_MIN]
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0xA350}, 	// MCU_ADDRESS [AWB_CCM_POSITION_MAX]
	{0x3390, 0x007F}, 	// MCU_DATA_0
	{0x338C, 0xA353}, 	// MCU_ADDRESS [AWB_MODE]
	{0x3390, 0x0002}, 	// MCU_DATA_0
	{0x338C, 0xA35B}, 	// MCU_ADDRESS [AWB_STEADY_BGAIN_OUT_MIN]
	{0x3390, 0x0078}, 	// MCU_DATA_0
	{0x338C, 0xA35C}, 	// MCU_ADDRESS [AWB_STEADY_BGAIN_OUT_MAX]
	{0x3390, 0x0086}, 	// MCU_DATA_0
	{0x338C, 0xA35D}, 	// MCU_ADDRESS [AWB_STEADY_BGAIN_IN_MIN]
	{0x3390, 0x007E}, 	// MCU_DATA_0
	{0x338C, 0xA35E}, 	// MCU_ADDRESS [AWB_STEADY_BGAIN_IN_MAX]
	{0x3390, 0x0082}, 	// MCU_DATA_0
	{0x338C, 0x235F}, 	// MCU_ADDRESS [AWB_CNT_PXL_TH]
	{0x3390, 0x0040}, 	// MCU_DATA_0
	{0x338C, 0xA361}, 	// MCU_ADDRESS [AWB_TG_MIN0]
	{0x3390, 0x00C8}, 	// MCU_DATA_0
	{0x338C, 0xA362}, 	// MCU_ADDRESS [AWB_TG_MAX0]
	{0x3390, 0x00E1}, 	// MCU_DATA_0
	{0x338C, 0xA302}, 	// MCU_ADDRESS [AWB_WINDOW_POS]
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0xA303}, 	// MCU_ADDRESS [AWB_WINDOW_SIZE]
	{0x3390, 0x00EF}, 	// MCU_DATA_0
	{0x338C, 0xA352}, 	// MCU_ADDRESS [AWB_SATURATION]
	{0x3390, 0x001E}, 	// MCU_DATA_0
	{0x338C, 0xA118}, 	// MCU_ADDRESS [SEQ_LLSAT1]
	{0x3390, 0x001E}, 	// MCU_DATA_0
	{0x338C, 0xA103}, 	// MCU_ADDRESS [SEQ_CMD]
	{0x3390, 0x0005}, 	// MCU_DATA_0
	 //Contrast
	{0x338C, 0xA76D}, 	// MCU_ADDRESS [MODE_GAM_CONT_A]
	{0x3390, 0x0003}, 	// MCU_DATA_0
	{0x338C, 0xA76F}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_0]
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0xA770}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_1]
	{0x3390, 0x000B}, 	// MCU_DATA_0
	{0x338C, 0xA771}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_2]
	{0x3390, 0x0023}, 	// MCU_DATA_0
	{0x338C, 0xA772}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_3]
	{0x3390, 0x0043}, 	// MCU_DATA_0
	{0x338C, 0xA773}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_4]
	{0x3390, 0x006E}, 	// MCU_DATA_0
	{0x338C, 0xA774}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_5]
	{0x3390, 0x0090}, 	// MCU_DATA_0
	{0x338C, 0xA775}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_6]
	{0x3390, 0x00A8}, 	// MCU_DATA_0
	{0x338C, 0xA776}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_7]
	{0x3390, 0x00B9}, 	// MCU_DATA_0
	{0x338C, 0xA777}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_8]
	{0x3390, 0x00C6}, 	// MCU_DATA_0
	{0x338C, 0xA778}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_9]
	{0x3390, 0x00D0}, 	// MCU_DATA_0
	{0x338C, 0xA779}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_10]
	{0x3390, 0x00D9}, 	// MCU_DATA_0
	{0x338C, 0xA77A}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_11]
	{0x3390, 0x00E0}, 	// MCU_DATA_0
	{0x338C, 0xA77B}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_12]
	{0x3390, 0x00E6}, 	// MCU_DATA_0
	{0x338C, 0xA77C}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_13]
	{0x3390, 0x00EB}, 	// MCU_DATA_0
	{0x338C, 0xA77D}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_14]
	{0x3390, 0x00F0}, 	// MCU_DATA_0
	{0x338C, 0xA77E}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_15]
	{0x3390, 0x00F4}, 	// MCU_DATA_0
	{0x338C, 0xA77F}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_16]
	{0x3390, 0x00F8}, 	// MCU_DATA_0
	{0x338C, 0xA780}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_17]
	{0x3390, 0x00FC}, 	// MCU_DATA_0
	{0x338C, 0xA781}, 	// MCU_ADDRESS [MODE_GAM_TABLE_A_18]
	{0x3390, 0x00FF}, 	// MCU_DATA_0
	//  into outdoor mode
	{0x338C, 0xA102},	// MCU_ADDRESS
	{0x3390, 0x002F},	// MCU_DATA_0
	{0x338C, 0xA114},	// MCU_ADDRESS
	{0x3390, 0x0005},	// MCU_DATA_0
	{SEQUENCE_WAIT_MS, 20},
	{0x338C, 0xA103},  // MCU_ADDRESS
	{0x3390, 0x0005},  // MCU_DATA_0
	//Exposure
	{0x338C, 0xA206},	// MCU_ADDRESS [AE_TARGET]
	{0x3390, 0x003C},	// MCU_DATA_0
	{SEQUENCE_END, 0x00}
};

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
	{SEQUENCE_PROPERTY, SEQUENCE_CAPTURE},
	{0x338C, 0x275F},
	{0x3390, 0x0000},
	{0x338C, 0x2761},
	{0x3390, 0x0640},
	{0x338C, 0x2763},
	{0x3390, 0x0000},
	{0x338C, 0x2765},
	{0x3390, 0x04B0},
	{0x338C, 0x2707},
	{0x3390, 0x0640},
	{0x338C, 0x2709},
	{0x3390, 0x04B0},
	{0x338C, 0xA103},
	{0x3390, 0x0005},
	{SEQUENCE_END, 0x00}
};
/* 1280X1024 SXGA */
static struct reginfo sensor_sxga[] =
{
	{SEQUENCE_PROPERTY, SEQUENCE_CAPTURE},
	{0x338C, 0x275F},
	{0x3390, 0x0000},
	{0x338C, 0x2761},
	{0x3390, 0x0640},
	{0x338C, 0x2763},
	{0x3390, 0x0000},
	{0x338C, 0x2765},
	{0x3390, 0x04B0},
	{0x338C, 0x2707},
	{0x3390, 0x0500},
	{0x338C, 0x2709},
	{0x3390, 0x0400},
	{0x338C, 0xA103},
	{0x3390, 0x0005},
	{SEQUENCE_END, 0x00}
};
/*  1024X768 XGA */
static struct reginfo sensor_xga[] =
{
	{SEQUENCE_END, 0x00}
};

/* 800X600 SVGA*/
static struct reginfo sensor_svga[] =
{
	{SEQUENCE_END, 0x00}
};

/* 640X480 VGA */
static struct reginfo sensor_vga[] =
{
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
#if 0
/* 160X120 QQVGA*/
static struct reginfo ov2655_qqvga[] =
{

    {0x300E, 0x34},
    {0x3011, 0x01},
    {0x3012, 0x10},
    {0x302a, 0x02},
    {0x302b, 0xE6},
    {0x306f, 0x14},
    {0x3362, 0x90},

    {0x3070, 0x5d},
    {0x3072, 0x5d},
    {0x301c, 0x07},
    {0x301d, 0x07},

    {0x3020, 0x01},
    {0x3021, 0x18},
    {0x3022, 0x00},
    {0x3023, 0x06},
    {0x3024, 0x06},
    {0x3025, 0x58},
    {0x3026, 0x02},
    {0x3027, 0x61},
    {0x3088, 0x00},
    {0x3089, 0xa0},
    {0x308a, 0x00},
    {0x308b, 0x78},
    {0x3316, 0x64},
    {0x3317, 0x25},
    {0x3318, 0x80},
    {0x3319, 0x08},
    {0x331a, 0x0a},
    {0x331b, 0x07},
    {0x331c, 0x80},
    {0x331d, 0x38},
    {0x3100, 0x00},
    {0x3302, 0x11},

    {0x0, 0x0},
};



static  struct reginfo ov2655_Sharpness_auto[] =
{
    {0x3306, 0x00},
};

static  struct reginfo ov2655_Sharpness1[] =
{
    {0x3306, 0x08},
    {0x3371, 0x00},
};

static  struct reginfo ov2655_Sharpness2[][3] =
{
    //Sharpness 2
    {0x3306, 0x08},
    {0x3371, 0x01},
};

static  struct reginfo ov2655_Sharpness3[] =
{
    //default
    {0x3306, 0x08},
    {0x332d, 0x02},
};
static  struct reginfo ov2655_Sharpness4[]=
{
    //Sharpness 4
    {0x3306, 0x08},
    {0x332d, 0x03},
};

static  struct reginfo ov2655_Sharpness5[] =
{
    //Sharpness 5
    {0x3306, 0x08},
    {0x332d, 0x04},
};
#endif

static  struct reginfo sensor_Preview2Capture[]=
{
	{0x338C, 0xA120}, 	// MCU_ADDRESS [SEQ_CAP_MODE]
	{0x3390, 0x0002}, 	// MCU_DATA_0
	{0x338C, 0xA103}, 	// MCU_ADDRESS [SEQ_CMD]
	{0x3390, 0x0002}, 	// MCU_DATA_0
    {0x338C, 0xA102}, 	// MCU_ADDRESS [SEQ_CMD]  cgz oppo 2008-09-26
	{0x3390, 0x000E},
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Capture2Preview[]=
{
	{0x338C, 0xA120}, 	// MCU_ADDRESS [SEQ_CAP_MODE]
	{0x3390, 0x0000}, 	// MCU_DATA_0
	{0x338C, 0xA103}, 	// MCU_ADDRESS [SEQ_CMD]
	{0x3390, 0x0001}, 	// MCU_DATA_0
	{SEQUENCE_END, 0x00}
};

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
	//Auto
	{0x338C, 0xA34A},	// MCU_ADDRESS [AWB_GAIN_MIN]
	{0x3390, 0x0059},	// MCU_DATA_0
	{0x338C, 0xA34B},	// MCU_ADDRESS [AWB_GAIN_MAX]
	{0x3390, 0x00A6},	// MCU_DATA_0
	{0x338C, 0xA34F},	// MCU_ADDRESS [AWB_CCM_POSITION_MIN]
	{0x3390, 0x0000},	// MCU_DATA_0
	{0x338C, 0xA350},	// MCU_ADDRESS [AWB_CCM_POSITION_MAX]
	{0x3390, 0x007F},	// MCU_DATA_0
	{SEQUENCE_END, 0x00}
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static  struct reginfo sensor_WhiteB_Cloudy[]=
{
	//[V.       DL 7500]
	{0x338C, 0xA34B},	// MCU_ADDRESS [AWB_GAIN_MAX]
	{0x3390, 0x0080},	// MCU_DATA_0
	{0x338C, 0xA34F},	// MCU_ADDRESS [AWB_CCM_POSITION_MIN]
	{0x3390, 0x007F},	// MCU_DATA_0
	{0x338C, 0xA350},	// MCU_ADDRESS [AWB_CCM_POSITION_MAX]
	{0x3390, 0x007F},	// MCU_DATA_
	{SEQUENCE_END, 0x00}
};
/* ClearDay Colour Temperature : 5000K - 6500K  */
static  struct reginfo sensor_WhiteB_ClearDay[]=
{
	//[IV       Day Light]
	{0x338C, 0xA34A},	// MCU_ADDRESS [AWB_GAIN_MIN]
	{0x3390, 0x0080},	// MCU_DATA_0
	{0x338C, 0xA34B},	// MCU_ADDRESS [AWB_GAIN_MAX]
	{0x3390, 0x0080},	// MCU_DATA_0
	{0x338C, 0xA34F},	// MCU_ADDRESS [AWB_CCM_POSITION_MIN]
	{0x3390, 0x007E},	 // MCU_DATA_0
	{0x338C, 0xA350},	// MCU_ADDRESS [AWB_CCM_POSITION_MAX]
	{0x3390, 0x007E},	 // MCU_DATA_0
	{SEQUENCE_END, 0x00}
};
/* Office Colour Temperature : 3500K - 5000K  */
static  struct reginfo sensor_WhiteB_TungstenLamp1[]=
{
	//[III        Fluorescent]
	{0x338C, 0xA34A},  // MCU_ADDRESS [AWB_GAIN_MIN]
	{0x3390, 0x0080},  // MCU_DATA_0
	{0x338C, 0xA34B},  // MCU_ADDRESS [AWB_GAIN_MAX]
	{0x3390, 0x0080},  // MCU_DATA_0
	{0x338C, 0xA34F},  // MCU_ADDRESS [AWB_CCM_POSITION_MIN]
	{0x3390, 0x0030},   // MCU_DATA_0
	{0x338C, 0xA350},  // MCU_ADDRESS [AWB_CCM_POSITION_MAX]
	{0x3390, 0x0030},   // MCU_DATA_0
	{SEQUENCE_END, 0x00}
};
/* Home Colour Temperature : 2500K - 3500K  */
static  struct reginfo sensor_WhiteB_TungstenLamp2[]=
{
	//[II.        Incandescent]
	{0x338C, 0xA34A},	// MCU_ADDRESS [AWB_GAIN_MIN]
	{0x3390, 0x0080},	// MCU_DATA_0
	{0x338C, 0xA34B},	// MCU_ADDRESS [AWB_GAIN_MAX]
	{0x3390, 0x0080},	// MCU_DATA_0
	{0x338C, 0xA34F},	// MCU_ADDRESS [AWB_CCM_POSITION_MIN]
	{0x3390, 0x0032},	 // MCU_DATA_0
	{0x338C, 0xA350},	// MCU_ADDRESS [AWB_CCM_POSITION_MAX]
	{0x3390, 0x0032},	 // MCU_DATA_0
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
    sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};
#endif

#if CONFIG_SENSOR_Brightness
static  struct reginfo sensor_Brightness0[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness1[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness2[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness3[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness4[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Brightness5[]=
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
    sensor_Brightness4, sensor_Brightness5,NULL,
};

#endif

#if CONFIG_SENSOR_Effect
static  struct reginfo sensor_Effect_Normal[] =
{
	{0x338C, 0x2799},	// MCU_ADDRESS [MODE_SPEC_EFFECTS_A]
	{0x3390, 0x6408},	// MCU_DATA_0
	{0x338C, 0x279B},	// MCU_ADDRESS [MODE_SPEC_EFFECTS_B]
	{0x3390, 0x6408},	// MCU_DATA_0
	{0x338C, 0xA103},	// MCU_ADDRESS [SEQ_CMD]
	{0x3390, 0x0005},	// MCU_DATA_0
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_WandB[] =
{
	{0x338C, 0x2799},	// MCU_ADDRESS [MODE_SPEC_EFFECTS_A]
	{0x3390, 0x6409},	// MCU_DATA_0
	{0x338C, 0x279B},	// MCU_ADDRESS [MODE_SPEC_EFFECTS_B]
	{0x3390, 0x6409},	// MCU_DATA_0
	{0x338C, 0xA103},	// MCU_ADDRESS [SEQ_CMD]
	{0x3390, 0x0005},	// MCU_DATA_0
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Sepia[] =
{
	{0x338C, 0x2799},	// MCU_ADDRESS [MODE_SPEC_EFFECTS_A]
	{0x3390, 0x640A},	// MCU_DATA_0
	{0x338C, 0x279B},	// MCU_ADDRESS [MODE_SPEC_EFFECTS_B]
	{0x3390, 0x640A},	// MCU_DATA_0
	{0x338C, 0xA103},	// MCU_ADDRESS [SEQ_CMD]
	{0x3390, 0x0005},	// MCU_DATA_0
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Negative[] =
{
	{0x338C, 0x2799},	// MCU_ADDRESS [MODE_SPEC_EFFECTS_A]
	{0x3390, 0x640B},	// MCU_DATA_0
	{0x338C, 0x279B},	// MCU_ADDRESS [MODE_SPEC_EFFECTS_B]
	{0x3390, 0x640B},	// MCU_DATA_0
	{0x338C, 0xA103},	// MCU_ADDRESS [SEQ_CMD]
	{0x3390, 0x0005},	// MCU_DATA_0
	{SEQUENCE_END, 0x00}
};
static  struct reginfo sensor_Effect_Bluish[] =
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Effect_Green[] =
{
	{SEQUENCE_END, 0x00}
};

static struct reginfo sensor_Effect_Solarize[] =
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Sepia,sensor_Effect_Negative,
    NULL,
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
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Saturation1[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Saturation2[]=
{
	{SEQUENCE_END, 0x00}
};
static struct reginfo *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

#endif
#if CONFIG_SENSOR_Contrast
static  struct reginfo sensor_Contrast0[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast1[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast2[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast3[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast4[]=
{
	{SEQUENCE_END, 0x00}
};


static  struct reginfo sensor_Contrast5[]=
{
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_Contrast6[]=
{
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
	{0x338C, 0xA102},         // MCU_ADDRESS
	{0x3390, 0x002F},         // MCU_DATA_0
	{0x338C, 0xA114},         // MCU_ADDRESS
	{0x3390, 0x0005},         // MCU_DATA_0
	{SEQUENCE_WAIT_MS, 20},
	{0x338C, 0xA103},  // MCU_ADDRESS
	{0x3390, 0x0005},  // MCU_DATA_0
	{SEQUENCE_END, 0x00}
};

static  struct reginfo sensor_SceneNight[] =
{
	{0x338C, 0xA102},     // MCU_ADDRESS
	{0x3390, 0x000F},     // MCU_DATA_0
	{SEQUENCE_WAIT_MS, 20},
	{0x338C, 0xA103},  // MCU_ADDRESS
	{0x3390, 0x0005},  // MCU_DATA_0
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
    #endif

	#if CONFIG_SENSOR_Scene
    { .id = V4L2_CID_SCENE,  .index = 0, .name = "auto", .reserved = 0,} ,{ .id = V4L2_CID_SCENE,  .index = 1,  .name = "night", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Flash
    { .id = V4L2_CID_FLASH,  .index = 0,  .name = "off",  .reserved = 0, }, {  .id = V4L2_CID_FLASH,  .index = 1, .name = "auto",  .reserved = 0,},
    { .id = V4L2_CID_FLASH,  .index = 2,  .name = "on", .reserved = 0,}, {  .id = V4L2_CID_FLASH, .index = 3,  .name = "torch", .reserved = 0,},
    #endif
};

static const struct v4l2_queryctrl sensor_controls[] =
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
        .maximum	= 3,
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
	{
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
    },
	{
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
#if CONFIG_SENSOR_Effect
static int sensor_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
#endif
#if CONFIG_SENSOR_WhiteBalance
static int sensor_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
#endif
static int sensor_deactivate(struct i2c_client *client);
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
enum sensor_work_state
{
	sensor_work_ready = 0,
	sensor_working,
};
struct sensor_work
{
	struct i2c_client *client;
	struct delayed_work dwork;
	enum sensor_work_state state;
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
    unsigned char mirror;                                        /* HFLIP */
    unsigned char flip;                                          /* VFLIP */
	bool snap2preview;
	bool video2preview;
	int capture_w;
	int capture_h;
	int preview_w;
	int preview_h;
    struct reginfo *winseqe_cur_addr;
	struct sensor_datafmt fmt;
	unsigned int enable;
	unsigned int funmodule_state;
} sensor_info_priv_t;



struct sensor_parameter
{
	unsigned short int preview_maxlines;
	unsigned short int preview_exposure;
	unsigned short int preview_line_width;
	unsigned short int preview_gain;

	unsigned short int capture_framerate;
	unsigned short int preview_framerate;
};

struct sensor
{
    struct v4l2_subdev subdev;
    struct i2c_client *client;
    sensor_info_priv_t info_priv;
	struct sensor_parameter parameter;
	struct workqueue_struct *sensor_wq;
	struct sensor_work sensor_wk;
	struct mutex wq_lock;
    int model;	/* V4L2_IDENT_OV* codes from v4l2-chip-ident.h */
#if CONFIG_SENSOR_I2C_NOSCHED
	atomic_t tasklock_cnt;
#endif
	struct rkcamera_platform_data *sensor_io_request;
    struct rkcamera_gpio_res *sensor_gpio_res;
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

/* sensor register write */
static int sensor_write(struct i2c_client *client, u16 reg, u16 val)
{
    int err=0,cnt;
    u8 buf[4];
    struct i2c_msg msg[1];

	switch (reg)
	{
		case SEQUENCE_WAIT_MS:
		{
			msleep(val);
			break;
		}

		case SEQUENCE_WAIT_US:
		{
			udelay(val);
			break;
		}

		case SEQUENCE_PROPERTY:
		{
			break;
		}
		default:
		{
		    buf[0] = reg >> 8;
		    buf[1] = reg & 0xFF;
		    buf[2] = val >> 8;
			buf[3] = val & 0xFF;

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
		}
	}
    return err;
}

/* sensor register read */
static int sensor_read(struct i2c_client *client, u16 reg, u16 *val)
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
    msg[1].len = 2;
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
#if CONFIG_SENSOR_I2C_RDWRCHK
	char valchk;
#endif

	cnt = 0;
	if (sensor_task_lock(client, 1) < 0)
		goto sensor_write_array_end;
    while (regarray[i].reg != SEQUENCE_END)
    {

        err = sensor_write(client, regarray[i].reg, regarray[i].val);
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
    while (regarray[i].reg != SEQUENCE_END)
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

static int sensor_af_single(struct i2c_client *client)
{
	int ret = 0;


sensor_af_single_end:
	return ret;
}

static int sensor_af_const(struct i2c_client *client)
{
	int ret = 0;

sensor_af_const_end:
	return ret;
}

static int sensor_af_zoneupdate(struct i2c_client *client)
{
	int ret = 0;

sensor_af_zoneupdate_end:
	return ret;
}

static int sensor_af_init(struct i2c_client *client)
{
	int ret = 0;

	return ret;
}
#endif

static int sensor_ioctrl(struct soc_camera_device *icd,enum rksensor_power_cmd cmd, int on)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	int ret = 0;

    SENSOR_DG("%s %s  cmd(%d) on(%d)\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd,on);
	switch (cmd)
	{
		case Sensor_PowerDown:
		{
			if (icl->powerdown) {
				ret = icl->powerdown(icd->pdev, on);
				if (ret == RK_CAM_IO_SUCCESS) {
					if (on == 0) {
						mdelay(2);
						if (icl->reset)
							icl->reset(icd->pdev);
					}
				} else if (ret == RK_CAM_EIO_REQUESTFAIL) {
					ret = -ENODEV;
					goto sensor_power_end;
				}
			}
			break;
		}
		case Sensor_Flash:
		{
			struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    		struct sensor *sensor = to_sensor(client);

			if (sensor->sensor_io_request && sensor->sensor_io_request->sensor_ioctrl) {
				sensor->sensor_io_request->sensor_ioctrl(icd->pdev,Cam_Flash, on);
			}
			break;
		}
		default:
		{
			SENSOR_TR("%s %s cmd(0x%x) is unknown!",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
			break;
		}
	}

sensor_power_end:
	return ret;
}
static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl;
    const struct sensor_datafmt *fmt;
	    int ret,i = 0,j=0;
#if (SENSOR_ID_REG != SEQUENCE_END)    
    u16 pid = 0;
#endif
   struct rkcamera_platform_data* tmp_plat_data =(struct rkcamera_platform_data*)val;
    sensor_init_data_p = sensor_init_data;
	sensor_init_winseq_p = sensor_svga;
	sensor_init_width = 800;
	sensor_init_height = 600;
	if (tmp_plat_data != NULL) { 
		for(i = 0;i < RK_CAM_NUM_PER_HOST;i++){
			if ((tmp_plat_data->sensor_init_data[i])&& tmp_plat_data->info[i].dev_name &&
				(strcmp(tmp_plat_data->info[i].dev_name, dev_name(icd->pdev)) == 0))
					break;
			}
		}
	if(tmp_plat_data  && (i < RK_CAM_NUM_PER_HOST) && tmp_plat_data->sensor_init_data[i]){
	//user has defined the init data
		//init reg
		if(tmp_plat_data->sensor_init_data[i]->rk_sensor_init_data && (sizeof(struct reginfo) != sizeof(struct reginfo_t))){
			for(j = 0;j< sizeof(sensor_init_data)/sizeof(struct reginfo);j++){
				sensor_init_data[j].reg = tmp_plat_data->sensor_init_data[i]->rk_sensor_init_data[j].reg;
				sensor_init_data[j].val = tmp_plat_data->sensor_init_data[i]->rk_sensor_init_data[j].val;
				}
			sensor_init_data_p = sensor_init_data;
			}
		else if(tmp_plat_data->sensor_init_data[i]->rk_sensor_init_data){
			sensor_init_data_p = (struct reginfo*)(tmp_plat_data->sensor_init_data[i]->rk_sensor_init_data);
			}
		//init winseq
		if(tmp_plat_data->sensor_init_data[i]->rk_sensor_init_winseq && (sizeof(struct reginfo) != sizeof(struct reginfo_t))){
			int tmp_winseq_size = tmp_plat_data->sensor_init_data[i]->rk_sensor_winseq_size;
			if(sensor_init_winseq_board)
				{
				vfree(sensor_init_winseq_board);
				sensor_init_winseq_board = NULL;
				}
			sensor_init_winseq_board = (struct reginfo*)vmalloc(tmp_winseq_size);
			if(!sensor_init_winseq_board)
				SENSOR_TR("%s :vmalloc erro !",__FUNCTION__);
			for(j = 0;j< tmp_winseq_size;j++){
				sensor_init_winseq_board[j].reg = tmp_plat_data->sensor_init_data[i]->rk_sensor_init_winseq[j].reg;
				sensor_init_winseq_board[j].val = tmp_plat_data->sensor_init_data[i]->rk_sensor_init_winseq[j].val;
				}
			sensor_init_winseq_p = sensor_init_winseq_board;
			}
		else if(tmp_plat_data->sensor_init_data[i]->rk_sensor_init_winseq){
			sensor_init_winseq_p = (struct reginfo*)(tmp_plat_data->sensor_init_data[i]->rk_sensor_init_winseq);
			}
		//init width,height,bus,pixelcode
		if(tmp_plat_data->sensor_init_data[i] && tmp_plat_data->sensor_init_data[i]->rk_sensor_init_width != INVALID_VALUE)
			sensor_init_width = tmp_plat_data->sensor_init_data[i]->rk_sensor_init_width;
		if(tmp_plat_data->sensor_init_data[i] && tmp_plat_data->sensor_init_data[i]->rk_sensor_init_height != INVALID_VALUE)
			sensor_init_height = tmp_plat_data->sensor_init_data[i]->rk_sensor_init_height;
		if(tmp_plat_data->sensor_init_data[i] && tmp_plat_data->sensor_init_data[i]->rk_sensor_init_bus_param != INVALID_VALUE)
			sensor_init_busparam = tmp_plat_data->sensor_init_data[i]->rk_sensor_init_bus_param;
		if(tmp_plat_data->sensor_init_data[i] && tmp_plat_data->sensor_init_data[i]->rk_sensor_init_pixelcode != INVALID_VALUE)
			sensor_init_pixelcode = tmp_plat_data->sensor_init_data[i]->rk_sensor_init_pixelcode;
	}


    SENSOR_DG("\n%s..%s.. \n",SENSOR_NAME_STRING(),__FUNCTION__);

	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_INIT_ERR;
	}

    /* soft reset */
	if (sensor_task_lock(client,1)<0)
		goto sensor_INIT_ERR;

#if (SENSOR_RESET_REG != SEQUENCE_END)
    ret = sensor_write(client, SENSOR_RESET_REG, SENSOR_RESET_VAL);
    if (ret != 0) {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_INIT_ERR;
    }

    mdelay(5);  //delay 5 microseconds
#endif

	/* check if it is an sensor sensor */
#if (SENSOR_ID_REG != SEQUENCE_END)
    ret = sensor_read(client, SENSOR_ID_REG, &pid);
    if (ret != 0) {
        SENSOR_TR("read chip id failed\n");
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }

    SENSOR_DG("\n %s  pid = 0x%x \n", SENSOR_NAME_STRING(), pid);
#else
	pid = SENSOR_ID;
#endif
    if (pid == SENSOR_ID) {
        sensor->model = SENSOR_V4L2_IDENT;
    } else {
        SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }

    ret = sensor_write_array(client, sensor_init_data_p);
    if (ret != 0)
    {
        SENSOR_TR("error: %s initial failed\n",SENSOR_NAME_STRING());
        goto sensor_INIT_ERR;
    }
	sensor_task_lock(client,0);
    sensor->info_priv.preview_w = SENSOR_INIT_WIDTH;
    sensor->info_priv.preview_h = SENSOR_INIT_HEIGHT;
    sensor->info_priv.capture_w = SENSOR_MAX_WIDTH;
    sensor->info_priv.capture_h = SENSOR_MAX_HEIGHT;
    sensor->info_priv.winseqe_cur_addr  = SENSOR_INIT_WINSEQADR;
    fmt = sensor_find_datafmt(SENSOR_INIT_PIXFMT,sensor_colour_fmts, ARRAY_SIZE(sensor_colour_fmts));
    if (!fmt) {
        SENSOR_TR("error: %s initial array colour fmts is not support!!",SENSOR_NAME_STRING());
        ret = -EINVAL;
        goto sensor_INIT_ERR;
    }
	sensor->info_priv.fmt = *fmt;

    /* sensor sensor information for initialization  */
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
	if (qctrl)
    	sensor->info_priv.whiteBalance = qctrl->default_value;
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
	#if CONFIG_SENSOR_Focus
    sensor_set_focus();
    qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.focus = qctrl->default_value;
	#endif

	#if CONFIG_SENSOR_Flash
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FLASH);
	if (qctrl)
        sensor->info_priv.flash = qctrl->default_value;
    #endif
    SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),((val == 0)?__FUNCTION__:"sensor_reinit"),icd->user_width,icd->user_height);

    return 0;
sensor_INIT_ERR:
	sensor_task_lock(client,0);
	sensor_deactivate(client);
    return ret;
}
static int sensor_deactivate(struct i2c_client *client)
{
	struct soc_camera_device *icd = client->dev.platform_data;

	SENSOR_DG("\n%s..%s.. Enter\n",SENSOR_NAME_STRING(),__FUNCTION__);

	/* ddl@rock-chips.com : all sensor output pin must change to input for other sensor */
	sensor_task_lock(client, 1);
	sensor_ioctrl(icd, Sensor_PowerDown, 1);

	/* ddl@rock-chips.com : sensor config init width , because next open sensor quickly(soc_camera_open -> Try to configure with default parameters) */
	icd->user_width = SENSOR_INIT_WIDTH;
    icd->user_height = SENSOR_INIT_HEIGHT;
	msleep(100);
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
            ret = sensor_ioctrl(icd, Sensor_PowerDown, 1);
            if (ret < 0) {
			    SENSOR_TR("\n %s suspend fail for turn on power!\n", SENSOR_NAME_STRING());
                return -EINVAL;
            }
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

    ret = sensor_ioctrl(icd, Sensor_PowerDown, 0);
    if (ret < 0) {
		SENSOR_TR("\n %s resume fail for turn on power!\n", SENSOR_NAME_STRING());
        return -EINVAL;
    }

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
{
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
{
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
static int sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
    const struct sensor_datafmt *fmt;
    struct reginfo *winseqe_set_addr=NULL;
    int ret = 0, set_w,set_h;

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

    if ((winseqe_set_addr  != sensor->info_priv.winseqe_cur_addr) && winseqe_set_addr) {
        ret |= sensor_write_array(client, winseqe_set_addr);
        if (ret != 0) {
            SENSOR_TR("%s set format capability failed\n", SENSOR_NAME_STRING());
            goto sensor_s_fmt_end;
        }
        sensor->info_priv.winseqe_cur_addr  = winseqe_set_addr;
		if ((winseqe_set_addr[0].reg==SEQUENCE_PROPERTY) && (winseqe_set_addr[0].val==SEQUENCE_CAPTURE)) {
        	SENSOR_DG("\n%s..%s..Capture icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),__FUNCTION__,set_w,set_h);
		} else {
			SENSOR_DG("\n%s..%s..Video icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),__FUNCTION__,set_w,set_h);
		}
    }

	if (winseqe_set_addr && (winseqe_set_addr[0].reg==SEQUENCE_PROPERTY) && (winseqe_set_addr[0].val==SEQUENCE_CAPTURE)) {
		ret |= sensor_write_array(client, sensor_Preview2Capture);
		if (ret != 0) {
        	SENSOR_TR("%s Preview 2 Capture failed\n", SENSOR_NAME_STRING());
        	goto sensor_s_fmt_end;
    	}
		sensor->info_priv.capture_w = set_w;
		sensor->info_priv.capture_h = set_h;
		sensor->info_priv.snap2preview = true;
	} else if (sensor->info_priv.snap2preview == true) {
		if (winseqe_set_addr || ((sensor->info_priv.preview_w == mf->width) && (sensor->info_priv.preview_h == mf->height))) {
			ret |= sensor_write_array(client, sensor_Capture2Preview);
			if (ret != 0) {
	        	SENSOR_TR("%s Capture 2 Preview failed\n", SENSOR_NAME_STRING());
	        	goto sensor_s_fmt_end;
	    	}
			sensor->info_priv.preview_w = mf->width;
			sensor->info_priv.preview_h = mf->height;
			sensor->info_priv.snap2preview = false;
		} else {
			SENSOR_TR("\n %s..%s Format is Invalidate. pix->width = %d.. pix->height = %d\n",SENSOR_NAME_STRING(),__FUNCTION__,mf->width,mf->height);
		}
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
    int ret = 0;
   
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
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
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
#if CONFIG_SENSOR_Flash
static int sensor_set_flash(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{    
    if ((value >= qctrl->minimum) && (value <= qctrl->maximum)) {
        if (value == 3) {       /* ddl@rock-chips.com: torch */
            sensor_ioctrl(icd, Sensor_Flash, Flash_Torch);   /* Flash On */
        } else {
            sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
        }
        SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
        return 0;
    }
    
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
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

sensor_set_focus_absolute_end:
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
sensor_set_focus_relative_end:
	return ret;
}

static int sensor_set_focus_mode(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
	struct sensor *sensor = to_sensor(client);
	int ret = 0;

	if ((sensor->info_priv.funmodule_state & SENSOR_AF_IS_OK)  && (sensor->info_priv.affm_reinit == 0)) {
		switch (value)
		{
			case SENSOR_AF_MODE_AUTO:
			{
				ret = sensor_af_single(client);
				break;
			}

			case SENSOR_AF_MODE_MACRO:
			{
				ret = sensor_set_focus_absolute(icd, qctrl, 0xff);
				break;
			}

			case SENSOR_AF_MODE_INFINITY:
			{
				ret = sensor_set_focus_absolute(icd, qctrl, 0x00);
				break;
			}

			case SENSOR_AF_MODE_CONTINUOUS:
			{
				ret = sensor_af_const(client);
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
static int sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
    const struct v4l2_queryctrl *qctrl;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
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
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
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
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
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
                ext_ctrl->value = sensor->info_priv.focus;
                break;
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
    int val_offset;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ext_ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
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

				if (sensor_set_focus_absolute(icd, qctrl,ext_ctrl->value) == 0) {
					if (ext_ctrl->value == qctrl->minimum) {
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_INFINITY;
					} else if (ext_ctrl->value == qctrl->maximum) {
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_MACRO;
					} else {
						sensor->info_priv.auto_focus = SENSOR_AF_MODE_FIXED;
					}
				}

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
				if (ext_ctrl->value == 1) {
					if (sensor_set_focus_mode(icd, qctrl,SENSOR_AF_MODE_AUTO) != 0)
						return -EINVAL;
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
						if (sensor_set_focus_mode(icd, qctrl,SENSOR_AF_MODE_CONTINUOUS) != 0)
							return -EINVAL;
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

	if (enable == 1) {
		sensor->info_priv.enable = 1;
	} else if (enable == 0) {
		sensor->info_priv.enable = 0;
	}
    
	return 0;
}

/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int sensor_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{
    int ret;
#if (SENSOR_ID_REG != SEQUENCE_END)
    u16 pid = 0;
#endif

    struct sensor *sensor = to_sensor(client);

    /* We must have a parent by now. And it cannot be a wrong one.
     * So this entire test is completely redundant. */
    if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_video_probe_err;
	}

    /* soft reset */
#if (SENSOR_RESET_REG != SEQUENCE_END)
    ret = sensor_write(client, SENSOR_RESET_REG, SENSOR_RESET_VAL);
    if (ret != 0) {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_INIT_ERR;
    }

    mdelay(5);  //delay 5 microseconds
#endif

	/* check if it is an sensor sensor */
#if (SENSOR_ID_REG != SEQUENCE_END)
    ret = sensor_read(client, SENSOR_ID_REG, &pid);
    if (ret != 0) {
        SENSOR_TR("read chip id failed\n");
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    SENSOR_DG("\n %s  pid = 0x%x \n", SENSOR_NAME_STRING(), pid);
#else
	pid = SENSOR_ID;
#endif

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
    int ret = 0;
#if CONFIG_SENSOR_Flash	
    int i;
#endif
    
	SENSOR_DG("\n%s..%s..cmd:%x \n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
	switch (cmd)
	{
		case RK_CAM_SUBDEV_DEACTIVATE:
		{
			sensor_deactivate(client);
			break;
		}
		case RK_CAM_SUBDEV_IOREQUEST:
		{
			sensor->sensor_io_request = (struct rkcamera_platform_data*)arg;
            if (sensor->sensor_io_request != NULL) { 
                if (sensor->sensor_io_request->gpio_res[0].dev_name && 
                    (strcmp(sensor->sensor_io_request->gpio_res[0].dev_name, dev_name(icd->pdev)) == 0)) {
                    sensor->sensor_gpio_res = (struct rkcamera_gpio_res*)&sensor->sensor_io_request->gpio_res[0];
                } else if (sensor->sensor_io_request->gpio_res[1].dev_name && 
                    (strcmp(sensor->sensor_io_request->gpio_res[1].dev_name, dev_name(icd->pdev)) == 0)) {
                    sensor->sensor_gpio_res = (struct rkcamera_gpio_res*)&sensor->sensor_io_request->gpio_res[1];
                }
            } else {
                SENSOR_TR("%s %s RK_CAM_SUBDEV_IOREQUEST fail\n",SENSOR_NAME_STRING(),__FUNCTION__);
                ret = -EINVAL;
                goto sensor_ioctl_end;
            }
            /* ddl@rock-chips.com : if gpio_flash havn't been set in board-xxx.c, sensor driver must notify is not support flash control 
               for this project */
            #if CONFIG_SENSOR_Flash	
        	if (sensor->sensor_gpio_res) { 
                if (sensor->sensor_gpio_res->gpio_flash == INVALID_GPIO) {
                    for (i = 0; i < icd->ops->num_controls; i++) {
                		if (V4L2_CID_FLASH == icd->ops->controls[i].id) {
                			memset((char*)&icd->ops->controls[i],0x00,sizeof(struct v4l2_queryctrl));                			
                		}
                    }
                    sensor->info_priv.flash = 0xff;
                    SENSOR_DG("%s flash gpio is invalidate!\n",SENSOR_NAME_STRING());
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
    }
    SENSOR_DG("\n%s..%s..%d  ret = %x \n",__FUNCTION__,__FILE__,__LINE__,ret);
    return ret;
}

static int sensor_remove(struct i2c_client *client)
{
    struct sensor *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;

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
    SENSOR_DG("\n%s..%s.. \n",__FUNCTION__,SENSOR_NAME_STRING());
    return i2c_add_driver(&sensor_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
    i2c_del_driver(&sensor_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION(SENSOR_NAME_STRING(Camera sensor driver));
MODULE_AUTHOR("ddl <kernel@rock-chips>");
MODULE_LICENSE("GPL");




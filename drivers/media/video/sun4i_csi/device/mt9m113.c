/*
 * drivers/media/video/sun4i_csi/device/mt9m113.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
 * A V4L2 driver for GalaxyCore mt9m113 cameras.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/clk.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-mediabus.h>//linux-3.0
#include <linux/io.h>
#include <plat/sys_config.h>
#include <linux/regulator/consumer.h>
#include <mach/system.h>
#include "../include/sun4i_csi_core.h"
#include "../include/sun4i_dev_csi.h"
#include <linux/io.h>
MODULE_AUTHOR("raymonxiu");
MODULE_DESCRIPTION("A low-level driver for GalaxyCore mt9m113 sensors");
MODULE_LICENSE("GPL");

//for internel driver debug
#define DEV_DBG_EN   		0
#if(DEV_DBG_EN == 1)
#define csi_dev_dbg(x,arg...) printk(KERN_INFO"[CSI_DEBUG][MT9M113]"x,##arg)
#else
#define csi_dev_dbg(x,arg...)
#endif
#define csi_dev_err(x,arg...) printk(KERN_INFO"[CSI_ERR][MT9M113]"x,##arg)
#define csi_dev_print(x,arg...) printk(KERN_INFO"[CSI][MT9M113]"x,##arg)

#define MCLK (24*1000*1000)
#define VREF_POL	CSI_HIGH
#define HREF_POL	CSI_HIGH
#define CLK_POL		CSI_FALLING
#define IO_CFG		0						//0 for csi0
#define V4L2_IDENT_SENSOR 0x113

//define the voltage level of control signal
#define CSI_STBY_ON			1
#define CSI_STBY_OFF 		0
#define CSI_RST_ON			0
#define CSI_RST_OFF			1
#define CSI_PWR_ON			1
#define CSI_PWR_OFF			0

#define REG_TERM 0xff
#define VAL_TERM 0xff


#define REG_ADDR_STEP 2
#define REG_DATA_STEP 2
#define REG_STEP 			(REG_ADDR_STEP+REG_DATA_STEP)

/*
 * Basic window sizes.  These probably belong somewhere more globally
 * useful.
 */
#define UXGA_WIDTH		1600
#define UXGA_HEIGHT		1200
#define SXGA_WIDTH		1280
#define SXGA_HEIGHT		1024
#define HD720_WIDTH 	1280
#define HD720_HEIGHT	720
#define SVGA_WIDTH		800
#define SVGA_HEIGHT 	600
#define VGA_WIDTH			640
#define VGA_HEIGHT		480
#define QVGA_WIDTH		320
#define QVGA_HEIGHT		240
#define CIF_WIDTH			352
#define CIF_HEIGHT		288
#define QCIF_WIDTH		176
#define	QCIF_HEIGHT		144

/*
 * Our nominal (default) frame rate.
 */
#define SENSOR_FRAME_RATE 30

/*
 * The mt9m113 sits on i2c with ID 0x78
 */
#define I2C_ADDR 0x78

/* Registers */


/*
 * Information we maintain about a known sensor.
 */
struct sensor_format_struct;  /* coming later */
struct snesor_colorfx_struct; /* coming later */
__csi_subdev_info_t ccm_info_con =
{
	.mclk 	= MCLK,
	.vref 	= VREF_POL,
	.href 	= HREF_POL,
	.clock	= CLK_POL,
	.iocfg	= IO_CFG,
};

struct sensor_info {
	struct v4l2_subdev sd;
	struct sensor_format_struct *fmt;  /* Current format */
	__csi_subdev_info_t *ccm_info;
	int	width;
	int	height;
	int brightness;
	int	contrast;
	int saturation;
	int hue;
	int hflip;
	int vflip;
	int gain;
	int autogain;
	int exp;
	enum v4l2_exposure_auto_type autoexp;
	int autowb;
	enum v4l2_whiteblance wb;
	enum v4l2_colorfx clrfx;
	enum v4l2_flash_mode flash_mode;
	u8 clkrc;			/* Clock divider value */
};

static inline struct sensor_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sensor_info, sd);
}



/*
 * The default register settings
 *
 */

struct regval_list {
	unsigned char reg_num[REG_ADDR_STEP];
	unsigned char value[REG_DATA_STEP];
};



static struct regval_list sensor_default_regs[] = {
//{ {0x00,0x1A}, {0x02,0x11} }, // RESET_AND_MISC_CONTROL
//{ {0xff,0xff}, {0x00,0x64} },
//{ {0x00,0x1A}, {0x02,0x10} }, // RESET_AND_MISC_CONTROL

{ {0x00,0x14}, {0xB0,0x47} }, // PLL_CONTROL
{ {0x00,0x14}, {0xB0,0x45} }, // PLL_CONTROL
{ {0x00,0x14}, {0x21,0x45} }, // PLL_CONTROL
{ {0x00,0x10}, {0x01,0x14} }, // PLL_DIVIDERS
{ {0x00,0x12}, {0x1F,0xF1} }, // PLL_P_DIVIDERS
{ {0x00,0x14}, {0x25,0x45} }, // PLL_CONTROL
{ {0x00,0x14}, {0x25,0x47} }, // PLL_CONTROL
{ {0x00,0x14}, {0x34,0x47} }, // PLL_CONTROL
{ {0x00,0x00}, {0x00,0x00} },

{ {0xff,0xff}, {0x00,0x64} }, //delay

{ {0x00,0x14}, {0x30,0x47} }, // PLL_CONTROL
{ {0x00,0x14}, {0x30,0x46} }, // PLL_CONTROL
{ {0x00,0x1A}, {0x02,0x10} }, // RESET_AND_MISC_CONTROL
{ {0x00,0x18}, {0x40,0x28} }, // STANDBY_CONTROL
{ {0x32,0x1C}, {0x00,0x03} }, // OFIFO_CONTROL_STATUS
////preview=640*480,capture=1280*1024,YUV MODE
{ {0x09,0x8C}, {0x27,0x03} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x02,0xa0} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x05} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x02,0x00} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x07} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x05,0x00} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x09} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x04,0x00} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x0D} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x10} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x0F} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x10} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x11} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x03,0xed} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x13} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x05,0x2D} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x15} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x21,0x11} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x17} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x04,0x6e} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x23} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x10} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x25} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x10} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x27} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x04,0x1B} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x29} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x05,0x2B} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x2B} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x21,0x11} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x2D} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x26} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x2F} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x4C} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x31} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0xF9} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x33} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0xA7} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x35} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x05,0x3D} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x37} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x07,0x22} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x39} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x00} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x3B} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x02,0x8F} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x3D} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x00} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x3F} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x01,0xeF} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x47} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x00} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x49} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x04,0xFF} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x4B} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x00} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x4D} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x03,0xFF} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA4,0x04} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x10} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA4,0x0D} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x02} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA4,0x0E} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x03} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA4,0x10} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x0A} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA1,0x03} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x06} },  // MCU_DATA_0
{ {0x00,0x00}, {0x00,0x00} },

{ {0xff,0xff}, {0x00,0x64} }, //delay

{ {0x09,0x8C}, {0xA1,0x03} },  // MCU_ADDRESS
{ {0x09,0x90}, {0x00,0x05} },  // MCU_DATA_0
{ {0x00,0x00}, {0x00,0x00} },

{ {0xff,0xff}, {0x00,0x64} }, //delay

{ {0x09,0x8C}, {0x22,0x2D} },  // MCU_ADDRESS [AE_R9_STEP]
{ {0x09,0x90}, {0x00,0x67} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA4,0x08} },  // MCU_ADDRESS [FD_SEARCH_F1_50]
{ {0x09,0x90}, {0x00,0x13} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA4,0x09} },  // MCU_ADDRESS [FD_SEARCH_F2_50]
{ {0x09,0x90}, {0x00,0x15} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA4,0x0A} },  // MCU_ADDRESS [FD_SEARCH_F1_60]
{ {0x09,0x90}, {0x00,0x17} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA4,0x0B} },  // MCU_ADDRESS [FD_SEARCH_F2_60]
{ {0x09,0x90}, {0x00,0x19} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x24,0x11} },  // MCU_ADDRESS [FD_R9_STEP_F60_A]
{ {0x09,0x90}, {0x00,0x67} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x24,0x13} },  // MCU_ADDRESS [FD_R9_STEP_F50_A]
{ {0x09,0x90}, {0x00,0x7C} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x24,0x15} },  // MCU_ADDRESS [FD_R9_STEP_F60_B]
{ {0x09,0x90}, {0x00,0x67} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x24,0x17} },  // MCU_ADDRESS [FD_R9_STEP_F50_B]
{ {0x09,0x90}, {0x00,0x7C} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xAB,0x2D} },  // MCU_ADDRESS [HG_NR_START_G]
{ {0x09,0x90}, {0x00,0x2A} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xAB,0x31} },  // MCU_ADDRESS [HG_NR_STOP_G]
{ {0x09,0x90}, {0x00,0x2E} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x2B,0x28} },  // MCU_ADDRESS [HG_LL_BRIGHTNESSSTART]
{ {0x09,0x90}, {0x1F,0x40} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x2B,0x2A} },  // MCU_ADDRESS [HG_LL_BRIGHTNESSSTOP]
{ {0x09,0x90}, {0x3A,0x98} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x2B,0x38} },  // MCU_ADDRESS [HG_GAMMASTARTMORPH]
{ {0x09,0x90}, {0x1F,0x40} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x2B,0x3A} },  // MCU_ADDRESS [HG_GAMMASTOPMORPH]
{ {0x09,0x90}, {0x3A,0x98} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x22,0x57} },  // MCU_ADDRESS [RESERVED_AE_57]
{ {0x09,0x90}, {0x27,0x10} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x22,0x50} },  // MCU_ADDRESS [RESERVED_AE_50]
{ {0x09,0x90}, {0x1B,0x58} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x22,0x52} },  // MCU_ADDRESS [RESERVED_AE_52]
{ {0x09,0x90}, {0x32,0xC8} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x19} },  // MCU_ADDRESS [MODE_SENSOR_FINE_CORRECTION_A]
{ {0x09,0x90}, {0x00,0xAC} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x1B} },  // MCU_ADDRESS [MODE_SENSOR_FINE_IT_MIN_A]
{ {0x09,0x90}, {0x01,0xF1} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x1D} },  // MCU_ADDRESS [MODE_SENSOR_FINE_IT_MAX_MARGIN_A]
{ {0x09,0x90}, {0x01,0x3F} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x1F} },  // MCU_ADDRESS [MODE_SENSOR_FRAME_LENGTH_A]
{ {0x09,0x90}, {0x02,0x93} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x21} },  // MCU_ADDRESS [MODE_SENSOR_LINE_LENGTH_PCK_A]
{ {0x09,0x90}, {0x06,0x80} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x5F} },  // MCU_ADDRESS [RESERVED_MODE_5F]
{ {0x09,0x90}, {0x05,0x96} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x61} },  // MCU_ADDRESS [RESERVED_MODE_61]
{ {0x09,0x90}, {0x00,0x94} },  // MCU_DATA_0
{ {0x36,0x4E}, {0x08,0xF0} },  // P_GR_P0Q0
{ {0x36,0x50}, {0x15,0x4F} },  // P_GR_P0Q1
{ {0x36,0x52}, {0x01,0x71} },  // P_GR_P0Q2
{ {0x36,0x54}, {0x2D,0x6E} },  // P_GR_P0Q3
{ {0x36,0x56}, {0x9E,0x2F} },  // P_GR_P0Q4
{ {0x36,0x58}, {0x02,0x70} },  // P_RD_P0Q0
{ {0x36,0x5A}, {0xD8,0x8B} },  // P_RD_P0Q1
{ {0x36,0x5C}, {0x0A,0x51} },  // P_RD_P0Q2
{ {0x36,0x5E}, {0x5C,0xEE} },  // P_RD_P0Q3
{ {0x36,0x60}, {0xBE,0x8F} },  // P_RD_P0Q4
{ {0x36,0x62}, {0x06,0x30} },  // P_BL_P0Q0
{ {0x36,0x64}, {0x01,0x8F} },  // P_BL_P0Q1
{ {0x36,0x66}, {0x49,0xB0} },  // P_BL_P0Q2
{ {0x36,0x68}, {0x0B,0x6C} },  // P_BL_P0Q3
{ {0x36,0x6A}, {0x8F,0xEF} },  // P_BL_P0Q4
{ {0x36,0x6C}, {0x02,0x50} },  // P_GB_P0Q0
{ {0x36,0x6E}, {0xBE,0x6A} },  // P_GB_P0Q1
{ {0x36,0x70}, {0x5A,0x50} },  // P_GB_P0Q2
{ {0x36,0x72}, {0x2D,0x6E} },  // P_GB_P0Q3
{ {0x36,0x74}, {0xE8,0x8E} },  // P_GB_P0Q4
{ {0x36,0x76}, {0x1A,0xCD} },  // P_GR_P1Q0
{ {0x36,0x78}, {0x44,0xAD} },  // P_GR_P1Q1
{ {0x36,0x7A}, {0x63,0xAD} },  // P_GR_P1Q2
{ {0x36,0x7C}, {0x83,0xCF} },  // P_GR_P1Q3
{ {0x36,0x7E}, {0x95,0x2F} },  // P_GR_P1Q4
{ {0x36,0x80}, {0x2F,0xCD} },  // P_RD_P1Q0
{ {0x36,0x82}, {0xE9,0x2B} },  // P_RD_P1Q1
{ {0x36,0x84}, {0x74,0xAE} },  // P_RD_P1Q2
{ {0x36,0x86}, {0x96,0x4E} },  // P_RD_P1Q3
{ {0x36,0x88}, {0xAB,0x0F} },  // P_RD_P1Q4
{ {0x36,0x8A}, {0x21,0x6C} },  // P_BL_P1Q0
{ {0x36,0x8C}, {0x3A,0xE8} },  // P_BL_P1Q1
{ {0x36,0x8E}, {0x77,0x4D} },  // P_BL_P1Q2
{ {0x36,0x90}, {0x9D,0x6E} },  // P_BL_P1Q3
{ {0x36,0x92}, {0x93,0xAF} },  // P_BL_P1Q4
{ {0x36,0x94}, {0x56,0x4C} },  // P_GB_P1Q0
{ {0x36,0x96}, {0x5A,0x6C} },  // P_GB_P1Q1
{ {0x36,0x98}, {0x20,0x0C} },  // P_GB_P1Q2
{ {0x36,0x9A}, {0x90,0xAE} },  // P_GB_P1Q3
{ {0x36,0x9C}, {0x86,0xAE} },  // P_GB_P1Q4
{ {0x36,0x9E}, {0x39,0x50} },  // P_GR_P2Q0
{ {0x36,0xA0}, {0x6F,0x8E} },  // P_GR_P2Q1
{ {0x36,0xA2}, {0xA2,0xB0} },  // P_GR_P2Q2
{ {0x36,0xA4}, {0x8C,0x6F} },  // P_GR_P2Q3
{ {0x36,0xA6}, {0x7F,0xCD} },  // P_GR_P2Q4
{ {0x36,0xA8}, {0x62,0x90} },  // P_RD_P2Q0
{ {0x36,0xAA}, {0xE8,0xEA} },  // P_RD_P2Q1
{ {0x36,0xAC}, {0xF6,0xAF} },  // P_RD_P2Q2
{ {0x36,0xAE}, {0x04,0x30} },  // P_RD_P2Q3
{ {0x36,0xB0}, {0x41,0xCF} },  // P_RD_P2Q4
{ {0x36,0xB2}, {0x1F,0xD0} },  // P_BL_P2Q0
{ {0x36,0xB4}, {0x76,0x6D} },  // P_BL_P2Q1
{ {0x36,0xB6}, {0xD7,0x70} },  // P_BL_P2Q2
{ {0x36,0xB8}, {0x33,0x6F} },  // P_BL_P2Q3
{ {0x36,0xBA}, {0x55,0xB1} },  // P_BL_P2Q4
{ {0x36,0xBC}, {0x2B,0xB0} },  // P_GB_P2Q0
{ {0x36,0xBE}, {0xC8,0xED} },  // P_GB_P2Q1
{ {0x36,0xC0}, {0x83,0x10} },  // P_GB_P2Q2
{ {0x36,0xC2}, {0x12,0xAF} },  // P_GB_P2Q3
{ {0x36,0xC4}, {0x52,0x8E} },  // P_GB_P2Q4
{ {0x36,0xC6}, {0x17,0x0D} },  // P_GR_P3Q0
{ {0x36,0xC8}, {0x14,0xCD} },  // P_GR_P3Q1
{ {0x36,0xCA}, {0x0F,0xEF} },  // P_GR_P3Q2
{ {0x36,0xCC}, {0xB9,0x6F} },  // P_GR_P3Q3
{ {0x36,0xCE}, {0x9D,0xD1} },  // P_GR_P3Q4
{ {0x36,0xD0}, {0x4E,0xCD} },  // P_RD_P3Q0
{ {0x36,0xD2}, {0x57,0x2C} },  // P_RD_P3Q1
{ {0x36,0xD4}, {0xF3,0x4B} },  // P_RD_P3Q2
{ {0x36,0xD6}, {0x43,0x4E} },  // P_RD_P3Q3
{ {0x36,0xD8}, {0xEE,0x4F} },  // P_RD_P3Q4
{ {0x36,0xDA}, {0x25,0x09} },  // P_BL_P3Q0
{ {0x36,0xDC}, {0x87,0x6A} },  // P_BL_P3Q1
{ {0x36,0xDE}, {0x52,0x0F} },  // P_BL_P3Q2
{ {0x36,0xE0}, {0x3A,0xAD} },  // P_BL_P3Q3
{ {0x36,0xE2}, {0x85,0xF1} },  // P_BL_P3Q4
{ {0x36,0xE4}, {0xB4,0x2C} },  // P_GB_P3Q0
{ {0x36,0xE6}, {0x5C,0xCD} },  // P_GB_P3Q1
{ {0x36,0xE8}, {0x75,0x0F} },  // P_GB_P3Q2
{ {0x36,0xEA}, {0xA1,0x4E} },  // P_GB_P3Q3
{ {0x36,0xEC}, {0x9D,0xB1} },  // P_GB_P3Q4
{ {0x36,0xEE}, {0xD1,0x4F} },  // P_GR_P4Q0
{ {0x36,0xF0}, {0xC7,0x0D} },  // P_GR_P4Q1
{ {0x36,0xF2}, {0x03,0x72} },  // P_GR_P4Q2
{ {0x36,0xF4}, {0x86,0x11} },  // P_GR_P4Q3
{ {0x36,0xF6}, {0x9F,0x73} },  // P_GR_P4Q4
{ {0x36,0xF8}, {0xF4,0x2F} },  // P_RD_P4Q0
{ {0x36,0xFA}, {0x27,0x8E} },  // P_RD_P4Q1
{ {0x36,0xFC}, {0x79,0xB1} },  // P_RD_P4Q2
{ {0x36,0xFE}, {0xE1,0x71} },  // P_RD_P4Q3
{ {0x37,0x00}, {0xCF,0x73} },  // P_RD_P4Q4
{ {0x37,0x02}, {0x82,0x0E} },  // P_BL_P4Q0
{ {0x37,0x04}, {0x09,0x6E} },  // P_BL_P4Q1
{ {0x37,0x06}, {0x62,0x32} },  // P_BL_P4Q2
{ {0x37,0x08}, {0xE0,0xD1} },  // P_BL_P4Q3
{ {0x37,0x0A}, {0x87,0x74} },  // P_BL_P4Q4
{ {0x37,0x0C}, {0x90,0xAF} },  // P_GB_P4Q0
{ {0x37,0x0E}, {0x02,0x8F} },  // P_GB_P4Q1
{ {0x37,0x10}, {0x62,0xF1} },  // P_GB_P4Q2
{ {0x37,0x12}, {0xC9,0x70} },  // P_GB_P4Q3
{ {0x37,0x14}, {0xF1,0x32} },  // P_GB_P4Q4
{ {0x36,0x44}, {0x02,0x84} },  // POLY_ORIGIN_C
{ {0x36,0x42}, {0x02,0x04} },  // POLY_ORIGIN_R
{ {0x32,0x10}, {0x00,0xB8} },  // COLOR_PIPELINE_CONTROL
{ {0x09,0x8C}, {0x23,0x06} },  // MCU_ADDRESS [AWB_CCM_L_0]
{ {0x09,0x90}, {0x02,0x33} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x08} },  // MCU_ADDRESS [AWB_CCM_L_1]
{ {0x09,0x90}, {0xFF,0x0B} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x0A} },  // MCU_ADDRESS [AWB_CCM_L_2]
{ {0x09,0x90}, {0x00,0x24} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x0C} },  // MCU_ADDRESS [AWB_CCM_L_3]
{ {0x09,0x90}, {0xFF,0xC8} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x0E} },  // MCU_ADDRESS [AWB_CCM_L_4]
{ {0x09,0x90}, {0x01,0xDE} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x10} },  // MCU_ADDRESS [AWB_CCM_L_5]
{ {0x09,0x90}, {0xFF,0xBD} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x12} },  // MCU_ADDRESS [AWB_CCM_L_6]
{ {0x09,0x90}, {0x00,0x19} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x14} },  // MCU_ADDRESS [AWB_CCM_L_7]
{ {0x09,0x90}, {0xFF,0x2B} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x16} },  // MCU_ADDRESS [AWB_CCM_L_8]
{ {0x09,0x90}, {0x01,0xE8} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x18} },  // MCU_ADDRESS [AWB_CCM_L_9]
{ {0x09,0x90}, {0x00,0x24} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x1A} },  // MCU_ADDRESS [AWB_CCM_L_10]
{ {0x09,0x90}, {0x00,0x48} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x1C} },  // MCU_ADDRESS [AWB_CCM_RL_0]
{ {0x09,0x90}, {0xFE,0xF2} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x1E} },  // MCU_ADDRESS [AWB_CCM_RL_1]
{ {0x09,0x90}, {0x00,0xB1} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x20} },  // MCU_ADDRESS [AWB_CCM_RL_2]
{ {0x09,0x90}, {0x00,0x2C} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x22} },  // MCU_ADDRESS [AWB_CCM_RL_3]
{ {0x09,0x90}, {0x00,0x17} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x24} },  // MCU_ADDRESS [AWB_CCM_RL_4]
{ {0x09,0x90}, {0x00,0x09} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x26} },  // MCU_ADDRESS [AWB_CCM_RL_5]
{ {0x09,0x90}, {0xFF,0xB2} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x28} },  // MCU_ADDRESS [AWB_CCM_RL_6]
{ {0x09,0x90}, {0xFF,0xFD} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x2A} },  // MCU_ADDRESS [AWB_CCM_RL_7]
{ {0x09,0x90}, {0x00,0xCF} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x2C} },  // MCU_ADDRESS [AWB_CCM_RL_8]
{ {0x09,0x90}, {0xFF,0x34} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x2E} },  // MCU_ADDRESS [AWB_CCM_RL_9]
{ {0x09,0x90}, {0x00,0x10} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x30} },  // MCU_ADDRESS [AWB_CCM_RL_10]
{ {0x09,0x90}, {0xFF,0xDC} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x48} },  // MCU_ADDRESS [AWB_GAIN_BUFFER_SPEED]
{ {0x09,0x90}, {0x00,0x08} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x49} },  // MCU_ADDRESS [AWB_JUMP_DIVISOR]
{ {0x09,0x90}, {0x00,0x02} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x4A} },  // MCU_ADDRESS [AWB_GAIN_MIN]
{ {0x09,0x90}, {0x00,0x59} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x4B} },  // MCU_ADDRESS [AWB_GAIN_MAX]
{ {0x09,0x90}, {0x00,0xA6} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x51} },  // MCU_ADDRESS [AWB_CCM_POSITION_MIN]
{ {0x09,0x90}, {0x00,0x00} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x52} },  // MCU_ADDRESS [AWB_CCM_POSITION_MAX]
{ {0x09,0x90}, {0x00,0x7F} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x54} },  // MCU_ADDRESS [AWB_SATURATION]
{ {0x09,0x90}, {0x00,0x80} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x55} },  // MCU_ADDRESS [AWB_MODE]
{ {0x09,0x90}, {0x00,0x01} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x5D} },  // MCU_ADDRESS [AWB_STEADY_BGAIN_OUT_MIN]
{ {0x09,0x90}, {0x00,0x78} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x5E} },  // MCU_ADDRESS [AWB_STEADY_BGAIN_OUT_MAX]
{ {0x09,0x90}, {0x00,0x86} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x5F} },  // MCU_ADDRESS [AWB_STEADY_BGAIN_IN_MIN]
{ {0x09,0x90}, {0x00,0x7E} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x60} },  // MCU_ADDRESS [AWB_STEADY_BGAIN_IN_MAX]
{ {0x09,0x90}, {0x00,0x82} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x23,0x61} },  // MCU_ADDRESS [RESERVED_AWB_61]
{ {0x09,0x90}, {0x00,0x40} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x63} },  // MCU_ADDRESS [RESERVED_AWB_63]
{ {0x09,0x90}, {0x00,0xD2} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x64} },  // MCU_ADDRESS [RESERVED_AWB_64]
{ {0x09,0x90}, {0x00,0xEE} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x02} },  // MCU_ADDRESS [AWB_WINDOW_POS]
{ {0x09,0x90}, {0x00,0x00} },  // MCU_DATA_0
{ {0x09,0x8C}, {0xA3,0x03} },  // MCU_ADDRESS [AWB_WINDOW_SIZE]
{ {0x09,0x90}, {0x00,0xEF} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x55} },  // MCU_ADDRESS [YUV_SWAP]
{ {0x09,0x90}, {0x00,0x02} },  // MCU_DATA_0
{ {0x09,0x8C}, {0x27,0x57} },  // MCU_ADDRESS [YUV_SWAP]
{ {0x09,0x90}, {0x00,0x02} },  // MCU_DATA_0
{ {0x00,0x1E}, {0x04,0x01} }, // PAD_SLEW 700 707 400 403 701
{ {0x09,0x8C}, {0xA1,0x03} },  // MCU_ADDRESS [SEQ_CMD]
{ {0x09,0x90}, {0x00,0x06} },  // MCU_DATA_0
{ {0x00,0x00}, {0x00,0x00} },

{ {0xff,0xff}, {0x00,0x64} }, //delay

{ {0x09,0x8C}, {0xA1,0x03} },	// MCU_ADDRESS [SEQ_CMD]
{ {0x09,0x90}, {0x00,0x05} },	// MCU_DATA_0
{ {0x00,0x00}, {0x00,0x00} },

{ {0xff,0xff}, {0x00,0x64} }, //delay
};

static struct regval_list sensor_sxga_regs[] = {
	//Resoltion Setting : 1280*1024
{ {0x09,0x8C}, {0xA1,0x15} },	// MCU_ADDRESS [SEQ_CAP_MODE]
{ {0x09,0x90}, {0x00,0x02} },	// MCU_DATA_0
{ {0x09,0x8C}, {0xA1,0x16} }, // p1:0x16 frame number in capture mode
{ {0x09,0x90}, {0xff,0xff} },
{ {0x09,0x8C}, {0xA1,0x03} },	// MCU_ADDRESS [SEQ_CMD]
{ {0x09,0x90}, {0x00,0x02} },	// MCU_DATA_0
{ {0xff,0xff}, {0x00,0x64} }, //delay

{ {0x00,0x1E}, {0x04,0x02} }, // PAD_SLEW 700 707 400 403 701
{ {0x09,0x8C}, {0xA1,0x03} },  // MCU_ADDRESS [SEQ_CMD]
{ {0x09,0x90}, {0x00,0x06} },  // MCU_DATA_0
{ {0x00,0x00}, {0x00,0x00} },

{ {0xff,0xff}, {0x00,0x64} }, //delay

{ {0x09,0x8C}, {0xA1,0x03} },	// MCU_ADDRESS [SEQ_CMD]
{ {0x09,0x90}, {0x00,0x05} },	// MCU_DATA_0
{ {0x00,0x00}, {0x00,0x00} },

{ {0xff,0xff}, {0x00,0x64} }, //delay
};


static struct regval_list sensor_svga_regs[] = {
	//Resolution Setting : 800*600

};

static struct regval_list sensor_vga_regs[] = {
	//Resolution Setting : 640*480
{ {0x09,0x8C}, {0xA1,0x15} },	// MCU_ADDRESS [SEQ_CAP_MODE]
{ {0x09,0x90}, {0x00,0x02} },	// MCU_DATA_0
{ {0x09,0x8C}, {0xA1,0x03} },	// MCU_ADDRESS [SEQ_CMD]
{ {0x09,0x90}, {0x00,0x01} },	// MCU_DATA_0
{ {0xff,0xff}, {0x00,0x64} }, //delay

{ {0x00,0x1E}, {0x04,0x01} }, // PAD_SLEW 700 707 400 403 701
{ {0x09,0x8C}, {0xA1,0x03} },  // MCU_ADDRESS [SEQ_CMD]
{ {0x09,0x90}, {0x00,0x06} },  // MCU_DATA_0
{ {0x00,0x00}, {0x00,0x00} },

{ {0xff,0xff}, {0x00,0x64} }, //delay

{ {0x09,0x8C}, {0xA1,0x03} },	// MCU_ADDRESS [SEQ_CMD]
{ {0x09,0x90}, {0x00,0x05} },	// MCU_DATA_0
{ {0x00,0x00}, {0x00,0x00} },

{ {0xff,0xff}, {0x00,0x64} }, //delay
};



/*
 * The white balance settings
 * Here only tune the R G B channel gain.
 * The white balance enalbe bit is modified in sensor_s_autowb and sensor_s_wb
 */
static struct regval_list sensor_wb_auto_regs[] = {

};

static struct regval_list sensor_wb_cloud_regs[] = {

};

static struct regval_list sensor_wb_daylight_regs[] = {
	//tai yang guang

};

static struct regval_list sensor_wb_incandescence_regs[] = {
	//bai re guang

};

static struct regval_list sensor_wb_fluorescent_regs[] = {
	//ri guang deng

};

static struct regval_list sensor_wb_tungsten_regs[] = {
	//wu si deng

};

/*
 * The color effect settings
 */
static struct regval_list sensor_colorfx_none_regs[] = {

};

static struct regval_list sensor_colorfx_bw_regs[] = {

};

static struct regval_list sensor_colorfx_sepia_regs[] = {

};

static struct regval_list sensor_colorfx_negative_regs[] = {

};

static struct regval_list sensor_colorfx_emboss_regs[] = {
//NULL
};

static struct regval_list sensor_colorfx_sketch_regs[] = {
//NULL
};

static struct regval_list sensor_colorfx_sky_blue_regs[] = {

};

static struct regval_list sensor_colorfx_grass_green_regs[] = {

};

static struct regval_list sensor_colorfx_skin_whiten_regs[] = {
//NULL
};

static struct regval_list sensor_colorfx_vivid_regs[] = {
//NULL
};

/*
 * The brightness setttings
 */
static struct regval_list sensor_brightness_neg4_regs[] = {
//NULL
};

static struct regval_list sensor_brightness_neg3_regs[] = {
//NULL
};

static struct regval_list sensor_brightness_neg2_regs[] = {
//NULL
};

static struct regval_list sensor_brightness_neg1_regs[] = {
//NULL
};

static struct regval_list sensor_brightness_zero_regs[] = {
//NULL
};

static struct regval_list sensor_brightness_pos1_regs[] = {
//NULL
};

static struct regval_list sensor_brightness_pos2_regs[] = {
//NULL
};

static struct regval_list sensor_brightness_pos3_regs[] = {
//NULL
};

static struct regval_list sensor_brightness_pos4_regs[] = {
//NULL
};

/*
 * The contrast setttings
 */
static struct regval_list sensor_contrast_neg4_regs[] = {
//NULL
};

static struct regval_list sensor_contrast_neg3_regs[] = {
//NULL
};

static struct regval_list sensor_contrast_neg2_regs[] = {
//NULL
};

static struct regval_list sensor_contrast_neg1_regs[] = {
//NULL
};

static struct regval_list sensor_contrast_zero_regs[] = {
//NULL
};

static struct regval_list sensor_contrast_pos1_regs[] = {
//NULL
};

static struct regval_list sensor_contrast_pos2_regs[] = {
//NULL
};

static struct regval_list sensor_contrast_pos3_regs[] = {
//NULL
};

static struct regval_list sensor_contrast_pos4_regs[] = {
//NULL
};

/*
 * The saturation setttings
 */
static struct regval_list sensor_saturation_neg4_regs[] = {
//NULL
};

static struct regval_list sensor_saturation_neg3_regs[] = {
//NULL
};

static struct regval_list sensor_saturation_neg2_regs[] = {
//NULL
};

static struct regval_list sensor_saturation_neg1_regs[] = {
//NULL
};

static struct regval_list sensor_saturation_zero_regs[] = {
//NULL
};

static struct regval_list sensor_saturation_pos1_regs[] = {
//NULL
};

static struct regval_list sensor_saturation_pos2_regs[] = {
//NULL
};

static struct regval_list sensor_saturation_pos3_regs[] = {
//NULL
};

static struct regval_list sensor_saturation_pos4_regs[] = {
//NULL
};

/*
 * The exposure target setttings
 */
static struct regval_list sensor_ev_neg4_regs[] = {

};

static struct regval_list sensor_ev_neg3_regs[] = {

};

static struct regval_list sensor_ev_neg2_regs[] = {

};

static struct regval_list sensor_ev_neg1_regs[] = {

};

static struct regval_list sensor_ev_zero_regs[] = {

};

static struct regval_list sensor_ev_pos1_regs[] = {

};

static struct regval_list sensor_ev_pos2_regs[] = {

};

static struct regval_list sensor_ev_pos3_regs[] = {

};

static struct regval_list sensor_ev_pos4_regs[] = {

};


/*
 * Here we'll try to encapsulate the changes for just the output
 * video format.
 *
 */


static struct regval_list sensor_fmt_yuv422_yuyv[] = {

	//YCbYCr
	{ {0x09,0x8C}, {0x27,0x55} },  // MCU_ADDRESS [YUV_SWAP]
	{ {0x09,0x90}, {0x00,0x02} },  // MCU_DATA_0
	{ {0x09,0x8C}, {0x27,0x57} },  // MCU_ADDRESS [YUV_SWAP]
	{ {0x09,0x90}, {0x00,0x02} },  // MCU_DATA_0

	{ {0x09,0x8C}, {0xA1,0x03} },	// MCU_ADDRESS [SEQ_CMD]
	{ {0x09,0x90}, {0x00,0x05} },	// MCU_DATA_0
	{ {0x00,0x00}, {0x00,0x00} },
	{ {0xff,0xff}, {0x00,0x64} }, //delay
};


static struct regval_list sensor_fmt_yuv422_yvyu[] = {

	//YCrYCb
	{ {0x09,0x8C}, {0x27,0x55} },  // MCU_ADDRESS [YUV_SWAP]
	{ {0x09,0x90}, {0x00,0x03} },  // MCU_DATA_0
	{ {0x09,0x8C}, {0x27,0x57} },  // MCU_ADDRESS [YUV_SWAP]
	{ {0x09,0x90}, {0x00,0x03} },  // MCU_DATA_0

	{ {0x09,0x8C}, {0xA1,0x03} },	// MCU_ADDRESS [SEQ_CMD]
	{ {0x09,0x90}, {0x00,0x05} },	// MCU_DATA_0
	{ {0x00,0x00}, {0x00,0x00} },
	{ {0xff,0xff}, {0x00,0x64} }, //delay
};

static struct regval_list sensor_fmt_yuv422_vyuy[] = {

	//CrYCbY
	{ {0x09,0x8C}, {0x27,0x55} },  // MCU_ADDRESS [YUV_SWAP]
	{ {0x09,0x90}, {0x00,0x01} },  // MCU_DATA_0
	{ {0x09,0x8C}, {0x27,0x57} },  // MCU_ADDRESS [YUV_SWAP]
	{ {0x09,0x90}, {0x00,0x01} },  // MCU_DATA_0

	{ {0x09,0x8C}, {0xA1,0x03} },	// MCU_ADDRESS [SEQ_CMD]
	{ {0x09,0x90}, {0x00,0x05} },	// MCU_DATA_0
	{ {0x00,0x00}, {0x00,0x00} },
	{ {0xff,0xff}, {0x00,0x64} }, //delay
};

static struct regval_list sensor_fmt_yuv422_uyvy[] = {

	//CbYCrY
	{ {0x09,0x8C}, {0x27,0x55} },  // MCU_ADDRESS [YUV_SWAP]
	{ {0x09,0x90}, {0x00,0x00} },  // MCU_DATA_0
	{ {0x09,0x8C}, {0x27,0x57} },  // MCU_ADDRESS [YUV_SWAP]
	{ {0x09,0x90}, {0x00,0x00} },  // MCU_DATA_0

	{ {0x09,0x8C}, {0xA1,0x03} },	// MCU_ADDRESS [SEQ_CMD]
	{ {0x09,0x90}, {0x00,0x05} },	// MCU_DATA_0
	{ {0x00,0x00}, {0x00,0x00} },
	{ {0xff,0xff}, {0x00,0x64} }, //delay
};

static struct regval_list sensor_fmt_raw[] = {

};



/*
 * Low-level register I/O.
 *
 */


/*
 * On most platforms, we'd rather do straight i2c I/O.
 */
static int sensor_read(struct v4l2_subdev *sd, unsigned char *reg,
		unsigned char *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 data[REG_STEP];
	struct i2c_msg msg;
	int ret,i;

	for(i = 0; i < REG_ADDR_STEP; i++)
		data[i] = reg[i];

	for(i = REG_ADDR_STEP; i < REG_STEP; i++)
		data[i] = 0xff;
	/*
	 * Send out the register address...
	 */
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = REG_ADDR_STEP;
	msg.buf = data;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		csi_dev_err("Error %d on register write\n", ret);
		return ret;
	}
	/*
	 * ...then read back the result.
	 */

	msg.flags = I2C_M_RD;
	msg.len = REG_DATA_STEP;
	msg.buf = &data[REG_ADDR_STEP];

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0) {
		for(i = 0; i < REG_DATA_STEP; i++)
			value[i] = data[i+REG_ADDR_STEP];
		ret = 0;
	}
	else {
		csi_dev_err("Error %d on register read\n", ret);
	}
	return ret;
}


static int sensor_write(struct v4l2_subdev *sd, unsigned char *reg,
		unsigned char *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char data[REG_STEP];
	int ret,i;

	for(i = 0; i < REG_ADDR_STEP; i++)
			data[i] = reg[i];
	for(i = REG_ADDR_STEP; i < REG_STEP; i++)
			data[i] = value[i-REG_ADDR_STEP];


	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = REG_STEP;
	msg.buf = data;


	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0) {
		ret = 0;
	}
	else if (ret < 0) {
		csi_dev_err("sensor_write error!\n");
	}
	return ret;
}



/*
 * Write a list of register settings;
 */
static int sensor_write_array(struct v4l2_subdev *sd, struct regval_list *vals , uint size)
{
	int i,ret;

	if (size == 0)
		return -EINVAL;
	for(i = 0; i < size ; i++)
	{
		if(vals->reg_num[0]==0xff && vals->reg_num[1]==0xff) {
				msleep(vals->value[0]);
		}
		else {
			ret = sensor_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				{
					csi_dev_err("sensor_write_err!\n");
					return ret;
				}
		}


		vals++;
	}

	return 0;
}


/*
 * Stuff that knows about the sensor.
 */

static int sensor_power(struct v4l2_subdev *sd, int on)
{
	struct csi_dev *dev=(struct csi_dev *)dev_get_drvdata(sd->v4l2_dev->dev);
	struct sensor_info *info = to_state(sd);
	char csi_stby_str[32],csi_power_str[32],csi_reset_str[32];

	if(info->ccm_info->iocfg == 0) {
		strcpy(csi_stby_str,"csi_stby");
		strcpy(csi_power_str,"csi_power_en");
		strcpy(csi_reset_str,"csi_reset");
	} else if(info->ccm_info->iocfg == 1) {
	  strcpy(csi_stby_str,"csi_stby_b");
	  strcpy(csi_power_str,"csi_power_en_b");
	  strcpy(csi_reset_str,"csi_reset_b");
	}

  switch(on)
	{
		case CSI_SUBDEV_STBY_ON:
			csi_dev_dbg("CSI_SUBDEV_STBY_ON\n");
			//reset off io
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_OFF,csi_reset_str);
			msleep(10);
			//active mclk before stadby in
			clk_enable(dev->csi_module_clk);
			msleep(100);
			//standby on io
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_STBY_ON,csi_stby_str);
			msleep(100);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_STBY_OFF,csi_stby_str);
			msleep(100);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_STBY_ON,csi_stby_str);
			msleep(100);
			//inactive mclk after stadby in
			clk_disable(dev->csi_module_clk);

			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_ON,csi_reset_str);
			msleep(10);
			break;
		case CSI_SUBDEV_STBY_OFF:
			csi_dev_dbg("CSI_SUBDEV_STBY_OFF\n");
			//active mclk before stadby out
			clk_enable(dev->csi_module_clk);
			msleep(10);
			//reset off io
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_OFF,csi_reset_str);
			msleep(10);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_ON,csi_reset_str);
			msleep(100);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_OFF,csi_reset_str);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_STBY_OFF,csi_stby_str);
			msleep(10);
			break;
		case CSI_SUBDEV_PWR_ON:
			csi_dev_dbg("CSI_SUBDEV_PWR_ON\n");
			//inactive mclk before power on
			clk_disable(dev->csi_module_clk);
			//power on reset
			gpio_set_one_pin_io_status(dev->csi_pin_hd,1,csi_stby_str);//set the gpio to output
			gpio_set_one_pin_io_status(dev->csi_pin_hd,1,csi_reset_str);//set the gpio to output
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_STBY_ON,csi_stby_str);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_ON,csi_reset_str);
			msleep(1);
			//power supply
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_PWR_ON,csi_power_str);
			msleep(10);
			if(dev->dvdd) {
				regulator_enable(dev->dvdd);
				msleep(10);
			}
			if(dev->avdd) {
				regulator_enable(dev->avdd);
				msleep(10);
			}
			if(dev->iovdd) {
				regulator_enable(dev->iovdd);
				msleep(10);
			}
			//active mclk before power on
			clk_enable(dev->csi_module_clk);
			//reset after power on
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_OFF,csi_reset_str);
			msleep(10);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_ON,csi_reset_str);
			msleep(100);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_OFF,csi_reset_str);
			msleep(100);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_STBY_OFF,csi_stby_str);
			msleep(10);
			break;

		case CSI_SUBDEV_PWR_OFF:
			csi_dev_dbg("CSI_SUBDEV_PWR_OFF\n");
			//power supply off
			if(dev->iovdd) {
				regulator_disable(dev->iovdd);
				msleep(10);
			}
			if(dev->avdd) {
				regulator_disable(dev->avdd);
				msleep(10);
			}
			if(dev->dvdd) {
				regulator_disable(dev->dvdd);
				msleep(10);
			}
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_PWR_OFF,csi_power_str);
			msleep(10);

			//inactive mclk after power off
			clk_disable(dev->csi_module_clk);

			//set the io to hi-z
			gpio_set_one_pin_io_status(dev->csi_pin_hd,0,csi_reset_str);//set the gpio to input
			gpio_set_one_pin_io_status(dev->csi_pin_hd,0,csi_stby_str);//set the gpio to input
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

static int sensor_reset(struct v4l2_subdev *sd, u32 val)
{
	struct csi_dev *dev=(struct csi_dev *)dev_get_drvdata(sd->v4l2_dev->dev);
	struct sensor_info *info = to_state(sd);
	char csi_reset_str[32];

	if(info->ccm_info->iocfg == 0) {
		strcpy(csi_reset_str,"csi_reset");
	} else if(info->ccm_info->iocfg == 1) {
	  strcpy(csi_reset_str,"csi_reset_b");
	}

	switch(val)
	{
		case CSI_SUBDEV_RST_OFF:
			csi_dev_dbg("CSI_SUBDEV_RST_OFF\n");
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_OFF,csi_reset_str);
			msleep(10);
			break;
		case CSI_SUBDEV_RST_ON:
			csi_dev_dbg("CSI_SUBDEV_RST_ON\n");
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_ON,csi_reset_str);
			msleep(10);
			break;
		case CSI_SUBDEV_RST_PUL:
			csi_dev_dbg("CSI_SUBDEV_RST_PUL\n");
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_OFF,csi_reset_str);
			msleep(10);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_ON,csi_reset_str);
			msleep(100);
			gpio_write_one_pin_value(dev->csi_pin_hd,CSI_RST_OFF,csi_reset_str);
			msleep(10);
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	int ret;
	struct regval_list regs;

	regs.reg_num[0] = 0x00;
	regs.reg_num[1] = 0x00;
	ret = sensor_read(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_read err at sensor_detect!\n");
		return ret;
	}

	if(regs.value[0] != 0x24)
		return -ENODEV;

	return 0;
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
	int ret;
	csi_dev_dbg("sensor_init\n");

	/*Make sure it is a target sensor*/
	ret = sensor_detect(sd);
	if (ret) {
		csi_dev_err("chip found is not an target chip.\n");
		return ret;
	}

	return sensor_write_array(sd, sensor_default_regs , ARRAY_SIZE(sensor_default_regs));
}

static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	int ret=0;

	switch(cmd){
		case CSI_SUBDEV_CMD_GET_INFO:
		{
			struct sensor_info *info = to_state(sd);
			__csi_subdev_info_t *ccm_info = arg;

			csi_dev_dbg("CSI_SUBDEV_CMD_GET_INFO\n");

			ccm_info->mclk 	=	info->ccm_info->mclk ;
			ccm_info->vref 	=	info->ccm_info->vref ;
			ccm_info->href 	=	info->ccm_info->href ;
			ccm_info->clock	=	info->ccm_info->clock;
			ccm_info->iocfg	=	info->ccm_info->iocfg;

			csi_dev_dbg("ccm_info.mclk=%x\n ",info->ccm_info->mclk);
			csi_dev_dbg("ccm_info.vref=%x\n ",info->ccm_info->vref);
			csi_dev_dbg("ccm_info.href=%x\n ",info->ccm_info->href);
			csi_dev_dbg("ccm_info.clock=%x\n ",info->ccm_info->clock);
			csi_dev_dbg("ccm_info.iocfg=%x\n ",info->ccm_info->iocfg);
			break;
		}
		case CSI_SUBDEV_CMD_SET_INFO:
		{
			struct sensor_info *info = to_state(sd);
			__csi_subdev_info_t *ccm_info = arg;

			csi_dev_dbg("CSI_SUBDEV_CMD_SET_INFO\n");

			info->ccm_info->mclk 	=	ccm_info->mclk 	;
			info->ccm_info->vref 	=	ccm_info->vref 	;
			info->ccm_info->href 	=	ccm_info->href 	;
			info->ccm_info->clock	=	ccm_info->clock	;
			info->ccm_info->iocfg	=	ccm_info->iocfg	;

			csi_dev_dbg("ccm_info.mclk=%x\n ",info->ccm_info->mclk);
			csi_dev_dbg("ccm_info.vref=%x\n ",info->ccm_info->vref);
			csi_dev_dbg("ccm_info.href=%x\n ",info->ccm_info->href);
			csi_dev_dbg("ccm_info.clock=%x\n ",info->ccm_info->clock);
			csi_dev_dbg("ccm_info.iocfg=%x\n ",info->ccm_info->iocfg);

			break;
		}
		default:
			return -EINVAL;
	}
		return ret;
}


/*
 * Store information about the video data format.
 */
static struct sensor_format_struct {
	__u8 *desc;
	//__u32 pixelformat;
	enum v4l2_mbus_pixelcode mbus_code;//linux-3.0
	struct regval_list *regs;
	int	regs_size;
	int bpp;   /* Bytes per pixel */
} sensor_formats[] = {
	{
		.desc		= "YUYV 4:2:2",
		.mbus_code	= V4L2_MBUS_FMT_YUYV8_2X8,//linux-3.0
		.regs 		= sensor_fmt_yuv422_yuyv,
		.regs_size = ARRAY_SIZE(sensor_fmt_yuv422_yuyv),
		.bpp		= 2,
	},
	{
		.desc		= "YVYU 4:2:2",
		.mbus_code	= V4L2_MBUS_FMT_YVYU8_2X8,//linux-3.0
		.regs 		= sensor_fmt_yuv422_yvyu,
		.regs_size = ARRAY_SIZE(sensor_fmt_yuv422_yvyu),
		.bpp		= 2,
	},
	{
		.desc		= "UYVY 4:2:2",
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,//linux-3.0
		.regs 		= sensor_fmt_yuv422_uyvy,
		.regs_size = ARRAY_SIZE(sensor_fmt_yuv422_uyvy),
		.bpp		= 2,
	},
	{
		.desc		= "VYUY 4:2:2",
		.mbus_code	= V4L2_MBUS_FMT_VYUY8_2X8,//linux-3.0
		.regs 		= sensor_fmt_yuv422_vyuy,
		.regs_size = ARRAY_SIZE(sensor_fmt_yuv422_vyuy),
		.bpp		= 2,
	},
	{
		.desc		= "Raw RGB Bayer",
		.mbus_code	= V4L2_MBUS_FMT_SBGGR8_1X8,//linux-3.0
		.regs 		= sensor_fmt_raw,
		.regs_size = ARRAY_SIZE(sensor_fmt_raw),
		.bpp		= 1
	},
};
#define N_FMTS ARRAY_SIZE(sensor_formats)



/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */


static struct sensor_win_size {
	int	width;
	int	height;
	int	hstart;		/* Start/stop values for the camera.  Note */
	int	hstop;		/* that they do not always make complete */
	int	vstart;		/* sense to humans, but evidently the sensor */
	int	vstop;		/* will do the right thing... */
	struct regval_list *regs; /* Regs to tweak */
	int regs_size;
	int (*set_size) (struct v4l2_subdev *sd);
/* h/vref stuff */
} sensor_win_sizes[] = {
	/* SXGA */
	{
		.width			= SXGA_WIDTH,
		.height			= SXGA_HEIGHT,
		.regs 			= sensor_sxga_regs,
		.regs_size	= ARRAY_SIZE(sensor_sxga_regs),
		.set_size		= NULL,
	},
	/* SVGA */
	{
		.width			= SVGA_WIDTH,
		.height			= SVGA_HEIGHT,
		.regs				= sensor_svga_regs,
		.regs_size	= ARRAY_SIZE(sensor_svga_regs),
		.set_size		= NULL,
	},
	/* VGA */
	{
		.width			= VGA_WIDTH,
		.height			= VGA_HEIGHT,
		.regs				= sensor_vga_regs,
		.regs_size	= ARRAY_SIZE(sensor_vga_regs),
		.set_size		= NULL,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))




static int sensor_enum_fmt(struct v4l2_subdev *sd, unsigned index,
                 enum v4l2_mbus_pixelcode *code)//linux-3.0
{
//	struct sensor_format_struct *ofmt;

	if (index >= N_FMTS)//linux-3.0
		return -EINVAL;

	*code = sensor_formats[index].mbus_code;//linux-3.0
//	ofmt = sensor_formats + fmt->index;
//	fmt->flags = 0;
//	strcpy(fmt->description, ofmt->desc);
//	fmt->pixelformat = ofmt->pixelformat;
	return 0;
}


static int sensor_try_fmt_internal(struct v4l2_subdev *sd,
		//struct v4l2_format *fmt,
		struct v4l2_mbus_framefmt *fmt,//linux-3.0
		struct sensor_format_struct **ret_fmt,
		struct sensor_win_size **ret_wsize)
{
	int index;
	struct sensor_win_size *wsize;
//	struct v4l2_pix_format *pix = &fmt->fmt.pix;//linux-3.0
	csi_dev_dbg("sensor_try_fmt_internal\n");
	for (index = 0; index < N_FMTS; index++)
		if (sensor_formats[index].mbus_code == fmt->code)//linux-3.0
			break;

	if (index >= N_FMTS) {
		/* default to first format */
		index = 0;
		fmt->code = sensor_formats[0].mbus_code;//linux-3.0
	}

	if (ret_fmt != NULL)
		*ret_fmt = sensor_formats + index;

	/*
	 * Fields: the sensor devices claim to be progressive.
	 */
	fmt->field = V4L2_FIELD_NONE;//linux-3.0


	/*
	 * Round requested image size down to the nearest
	 * we support, but not below the smallest.
	 */
	for (wsize = sensor_win_sizes; wsize < sensor_win_sizes + N_WIN_SIZES;
	     wsize++)
		if (fmt->width >= wsize->width && fmt->height >= wsize->height)//linux-3.0
			break;

	if (wsize >= sensor_win_sizes + N_WIN_SIZES)
		wsize--;   /* Take the smallest one */
	if (ret_wsize != NULL)
		*ret_wsize = wsize;
	/*
	 * Note the size we'll actually handle.
	 */
	fmt->width = wsize->width;//linux-3.0
	fmt->height = wsize->height;//linux-3.0
	//pix->bytesperline = pix->width*sensor_formats[index].bpp;//linux-3.0
	//pix->sizeimage = pix->height*pix->bytesperline;//linux-3.0

	return 0;
}

static int sensor_try_fmt(struct v4l2_subdev *sd,
             struct v4l2_mbus_framefmt *fmt)//linux-3.0
{
	return sensor_try_fmt_internal(sd, fmt, NULL, NULL);
}

/*
 * Set a format.
 */
static int sensor_s_fmt(struct v4l2_subdev *sd,
             struct v4l2_mbus_framefmt *fmt)//linux-3.0
{
	int ret;
	struct sensor_format_struct *sensor_fmt;
	struct sensor_win_size *wsize;
	struct sensor_info *info = to_state(sd);
	csi_dev_dbg("sensor_s_fmt\n");
	ret = sensor_try_fmt_internal(sd, fmt, &sensor_fmt, &wsize);
	if (ret)
		return ret;


	sensor_write_array(sd, sensor_fmt->regs , sensor_fmt->regs_size);

	ret = 0;
	if (wsize->regs)
	{
		ret = sensor_write_array(sd, wsize->regs , wsize->regs_size);
		if (ret < 0)
			return ret;
	}

	if (wsize->set_size)
	{
		ret = wsize->set_size(sd);
		if (ret < 0)
			return ret;
	}

	info->fmt = sensor_fmt;
	info->width = wsize->width;
	info->height = wsize->height;

	return 0;
}

/*
 * Implement G/S_PARM.  There is a "high quality" mode we could try
 * to do someday; for now, we just do the frame rate tweak.
 */
static int sensor_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	return -EINVAL;
}

static int sensor_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	return -EINVAL;
}


/*
 * Code for dealing with controls.
 * fill with different sensor module
 * different sensor module has different settings here
 * if not support the follow function ,retrun -EINVAL
 */

/* *********************************************begin of ******************************************** */
static int sensor_queryctrl(struct v4l2_subdev *sd,
		struct v4l2_queryctrl *qc)
{
	/* Fill in min, max, step and default value for these controls. */
	/* see include/linux/videodev2.h for details */
	/* see sensor_s_parm and sensor_g_parm for the meaning of value */

	switch (qc->id) {
//	case V4L2_CID_BRIGHTNESS:
//		return v4l2_ctrl_query_fill(qc, -4, 4, 1, 1);
//	case V4L2_CID_CONTRAST:
//		return v4l2_ctrl_query_fill(qc, -4, 4, 1, 1);
//	case V4L2_CID_SATURATION:
//		return v4l2_ctrl_query_fill(qc, -4, 4, 1, 1);
//	case V4L2_CID_HUE:
//		return v4l2_ctrl_query_fill(qc, -180, 180, 5, 0);
//	case V4L2_CID_VFLIP:
//	case V4L2_CID_HFLIP:
//		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 0);
//	case V4L2_CID_GAIN:
//		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 128);
//	case V4L2_CID_AUTOGAIN:
//		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
//	case V4L2_CID_EXPOSURE:
//		return v4l2_ctrl_query_fill(qc, -4, 4, 1, 0);
//	case V4L2_CID_EXPOSURE_AUTO:
//		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 0);
//	case V4L2_CID_DO_WHITE_BALANCE:
//		return v4l2_ctrl_query_fill(qc, 0, 5, 1, 0);
//	case V4L2_CID_AUTO_WHITE_BALANCE:
//		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
//	case V4L2_CID_COLORFX:
//		return v4l2_ctrl_query_fill(qc, 0, 9, 1, 0);
	case V4L2_CID_CAMERA_FLASH_MODE:
	  return v4l2_ctrl_query_fill(qc, 0, 4, 1, 0);
	}
	return -EINVAL;
}

static int sensor_g_hflip(struct v4l2_subdev *sd, __s32 *value)
{
	return -EINVAL;
}

static int sensor_s_hflip(struct v4l2_subdev *sd, int value)
{
	return -EINVAL;
}

static int sensor_g_vflip(struct v4l2_subdev *sd, __s32 *value)
{
	return -EINVAL;
}

static int sensor_s_vflip(struct v4l2_subdev *sd, int value)
{
	return -EINVAL;
}

static int sensor_g_autogain(struct v4l2_subdev *sd, __s32 *value)
{
	return -EINVAL;
}

static int sensor_s_autogain(struct v4l2_subdev *sd, int value)
{
	return -EINVAL;
}

static int sensor_g_autoexp(struct v4l2_subdev *sd, __s32 *value)
{
	return -EINVAL;
}

static int sensor_s_autoexp(struct v4l2_subdev *sd,
		enum v4l2_exposure_auto_type value)
{
	return -EINVAL;
}

static int sensor_g_autowb(struct v4l2_subdev *sd, int *value)
{
	return -EINVAL;
}

static int sensor_s_autowb(struct v4l2_subdev *sd, int value)
{
	int ret;

	ret = sensor_write_array(sd, sensor_wb_auto_regs, ARRAY_SIZE(sensor_wb_auto_regs));
	if (ret < 0) {
		csi_dev_err("sensor_write_array err at sensor_s_autowb!\n");
		return ret;
	}
	return 0;
}

static int sensor_g_hue(struct v4l2_subdev *sd, __s32 *value)
{
	return -EINVAL;
}

static int sensor_s_hue(struct v4l2_subdev *sd, int value)
{
	return -EINVAL;
}

static int sensor_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	return -EINVAL;
}

static int sensor_s_gain(struct v4l2_subdev *sd, int value)
{
	return -EINVAL;
}
/* *********************************************end of ******************************************** */

static int sensor_g_brightness(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);

	*value = info->brightness;
	return 0;
}

static int sensor_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	switch (value) {
		case -4:
		  ret = sensor_write_array(sd, sensor_brightness_neg4_regs, ARRAY_SIZE(sensor_brightness_neg4_regs));
			break;
		case -3:
			ret = sensor_write_array(sd, sensor_brightness_neg3_regs, ARRAY_SIZE(sensor_brightness_neg3_regs));
			break;
		case -2:
			ret = sensor_write_array(sd, sensor_brightness_neg2_regs, ARRAY_SIZE(sensor_brightness_neg2_regs));
			break;
		case -1:
			ret = sensor_write_array(sd, sensor_brightness_neg1_regs, ARRAY_SIZE(sensor_brightness_neg1_regs));
			break;
		case 0:
			ret = sensor_write_array(sd, sensor_brightness_zero_regs, ARRAY_SIZE(sensor_brightness_zero_regs));
			break;
		case 1:
			ret = sensor_write_array(sd, sensor_brightness_pos1_regs, ARRAY_SIZE(sensor_brightness_pos1_regs));
			break;
		case 2:
			ret = sensor_write_array(sd, sensor_brightness_pos2_regs, ARRAY_SIZE(sensor_brightness_pos2_regs));
			break;
		case 3:
			ret = sensor_write_array(sd, sensor_brightness_pos3_regs, ARRAY_SIZE(sensor_brightness_pos3_regs));
			break;
		case 4:
			ret = sensor_write_array(sd, sensor_brightness_pos4_regs, ARRAY_SIZE(sensor_brightness_pos4_regs));
			break;
		default:
			return -EINVAL;
	}

	if (ret < 0) {
		csi_dev_err("sensor_write_array err at sensor_s_brightness!\n");
		return ret;
	}
	msleep(10);
	info->brightness = value;
	return 0;
}

static int sensor_g_contrast(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);

	*value = info->contrast;
	return 0;
}

static int sensor_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	switch (value) {
		case -4:
		  ret = sensor_write_array(sd, sensor_contrast_neg4_regs, ARRAY_SIZE(sensor_contrast_neg4_regs));
			break;
		case -3:
			ret = sensor_write_array(sd, sensor_contrast_neg3_regs, ARRAY_SIZE(sensor_contrast_neg3_regs));
			break;
		case -2:
			ret = sensor_write_array(sd, sensor_contrast_neg2_regs, ARRAY_SIZE(sensor_contrast_neg2_regs));
			break;
		case -1:
			ret = sensor_write_array(sd, sensor_contrast_neg1_regs, ARRAY_SIZE(sensor_contrast_neg1_regs));
			break;
		case 0:
			ret = sensor_write_array(sd, sensor_contrast_zero_regs, ARRAY_SIZE(sensor_contrast_zero_regs));
			break;
		case 1:
			ret = sensor_write_array(sd, sensor_contrast_pos1_regs, ARRAY_SIZE(sensor_contrast_pos1_regs));
			break;
		case 2:
			ret = sensor_write_array(sd, sensor_contrast_pos2_regs, ARRAY_SIZE(sensor_contrast_pos2_regs));
			break;
		case 3:
			ret = sensor_write_array(sd, sensor_contrast_pos3_regs, ARRAY_SIZE(sensor_contrast_pos3_regs));
			break;
		case 4:
			ret = sensor_write_array(sd, sensor_contrast_pos4_regs, ARRAY_SIZE(sensor_contrast_pos4_regs));
			break;
		default:
			return -EINVAL;
	}

	if (ret < 0) {
		csi_dev_err("sensor_write_array err at sensor_s_contrast!\n");
		return ret;
	}
	msleep(10);
	info->contrast = value;
	return 0;
}

static int sensor_g_saturation(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);

	*value = info->saturation;
	return 0;
}

static int sensor_s_saturation(struct v4l2_subdev *sd, int value)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	switch (value) {
		case -4:
		  ret = sensor_write_array(sd, sensor_saturation_neg4_regs, ARRAY_SIZE(sensor_saturation_neg4_regs));
			break;
		case -3:
			ret = sensor_write_array(sd, sensor_saturation_neg3_regs, ARRAY_SIZE(sensor_saturation_neg3_regs));
			break;
		case -2:
			ret = sensor_write_array(sd, sensor_saturation_neg2_regs, ARRAY_SIZE(sensor_saturation_neg2_regs));
			break;
		case -1:
			ret = sensor_write_array(sd, sensor_saturation_neg1_regs, ARRAY_SIZE(sensor_saturation_neg1_regs));
			break;
		case 0:
			ret = sensor_write_array(sd, sensor_saturation_zero_regs, ARRAY_SIZE(sensor_saturation_zero_regs));
			break;
		case 1:
			ret = sensor_write_array(sd, sensor_saturation_pos1_regs, ARRAY_SIZE(sensor_saturation_pos1_regs));
			break;
		case 2:
			ret = sensor_write_array(sd, sensor_saturation_pos2_regs, ARRAY_SIZE(sensor_saturation_pos2_regs));
			break;
		case 3:
			ret = sensor_write_array(sd, sensor_saturation_pos3_regs, ARRAY_SIZE(sensor_saturation_pos3_regs));
			break;
		case 4:
			ret = sensor_write_array(sd, sensor_saturation_pos4_regs, ARRAY_SIZE(sensor_saturation_pos4_regs));
			break;
		default:
			return -EINVAL;
	}

	if (ret < 0) {
		csi_dev_err("sensor_write_array err at sensor_s_saturation!\n");
		return ret;
	}
	msleep(10);
	info->saturation = value;
	return 0;
}

static int sensor_g_exp(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);

	*value = info->exp;
	return 0;
}

static int sensor_s_exp(struct v4l2_subdev *sd, int value)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	switch (value) {
		case -4:
		  ret = sensor_write_array(sd, sensor_ev_neg4_regs, ARRAY_SIZE(sensor_ev_neg4_regs));
			break;
		case -3:
			ret = sensor_write_array(sd, sensor_ev_neg3_regs, ARRAY_SIZE(sensor_ev_neg3_regs));
			break;
		case -2:
			ret = sensor_write_array(sd, sensor_ev_neg2_regs, ARRAY_SIZE(sensor_ev_neg2_regs));
			break;
		case -1:
			ret = sensor_write_array(sd, sensor_ev_neg1_regs, ARRAY_SIZE(sensor_ev_neg1_regs));
			break;
		case 0:
			ret = sensor_write_array(sd, sensor_ev_zero_regs, ARRAY_SIZE(sensor_ev_zero_regs));
			break;
		case 1:
			ret = sensor_write_array(sd, sensor_ev_pos1_regs, ARRAY_SIZE(sensor_ev_pos1_regs));
			break;
		case 2:
			ret = sensor_write_array(sd, sensor_ev_pos2_regs, ARRAY_SIZE(sensor_ev_pos2_regs));
			break;
		case 3:
			ret = sensor_write_array(sd, sensor_ev_pos3_regs, ARRAY_SIZE(sensor_ev_pos3_regs));
			break;
		case 4:
			ret = sensor_write_array(sd, sensor_ev_pos4_regs, ARRAY_SIZE(sensor_ev_pos4_regs));
			break;
		default:
			return -EINVAL;
	}

	if (ret < 0) {
		csi_dev_err("sensor_write_array err at sensor_s_exp!\n");
		return ret;
	}
	msleep(10);
	info->exp = value;
	return 0;
}

static int sensor_g_wb(struct v4l2_subdev *sd, int *value)
{
	struct sensor_info *info = to_state(sd);
	enum v4l2_whiteblance *wb_type = (enum v4l2_whiteblance*)value;

	*wb_type = info->wb;

	return 0;
}

static int sensor_s_wb(struct v4l2_subdev *sd,
		enum v4l2_whiteblance value)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	if (value == V4L2_WB_AUTO) {
		ret = sensor_s_autowb(sd, 1);
		return ret;
	}
	else {
		ret = sensor_s_autowb(sd, 0);
		if(ret < 0) {
			csi_dev_err("sensor_s_autowb error, return %x!\n",ret);
			return ret;
		}

		switch (value) {
			case V4L2_WB_CLOUD:
			  ret = sensor_write_array(sd, sensor_wb_cloud_regs, ARRAY_SIZE(sensor_wb_cloud_regs));
				break;
			case V4L2_WB_DAYLIGHT:
				ret = sensor_write_array(sd, sensor_wb_daylight_regs, ARRAY_SIZE(sensor_wb_daylight_regs));
				break;
			case V4L2_WB_INCANDESCENCE:
				ret = sensor_write_array(sd, sensor_wb_incandescence_regs, ARRAY_SIZE(sensor_wb_incandescence_regs));
				break;
			case V4L2_WB_FLUORESCENT:
				ret = sensor_write_array(sd, sensor_wb_fluorescent_regs, ARRAY_SIZE(sensor_wb_fluorescent_regs));
				break;
			case V4L2_WB_TUNGSTEN:
				ret = sensor_write_array(sd, sensor_wb_tungsten_regs, ARRAY_SIZE(sensor_wb_tungsten_regs));
				break;
			default:
				return -EINVAL;
		}
	}

	if (ret < 0) {
		csi_dev_err("sensor_s_wb error, return %x!\n",ret);
		return ret;
	}

	msleep(10);
	info->wb = value;
	return 0;
}

static int sensor_g_colorfx(struct v4l2_subdev *sd,
		__s32 *value)
{
	struct sensor_info *info = to_state(sd);
	enum v4l2_colorfx *clrfx_type = (enum v4l2_colorfx*)value;

	*clrfx_type = info->clrfx;
	return 0;
}

static int sensor_s_colorfx(struct v4l2_subdev *sd,
		enum v4l2_colorfx value)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	switch (value) {
	case V4L2_COLORFX_NONE:
	  ret = sensor_write_array(sd, sensor_colorfx_none_regs, ARRAY_SIZE(sensor_colorfx_none_regs));
		break;
	case V4L2_COLORFX_BW:
		ret = sensor_write_array(sd, sensor_colorfx_bw_regs, ARRAY_SIZE(sensor_colorfx_bw_regs));
		break;
	case V4L2_COLORFX_SEPIA:
		ret = sensor_write_array(sd, sensor_colorfx_sepia_regs, ARRAY_SIZE(sensor_colorfx_sepia_regs));
		break;
	case V4L2_COLORFX_NEGATIVE:
		ret = sensor_write_array(sd, sensor_colorfx_negative_regs, ARRAY_SIZE(sensor_colorfx_negative_regs));
		break;
	case V4L2_COLORFX_EMBOSS:
		ret = sensor_write_array(sd, sensor_colorfx_emboss_regs, ARRAY_SIZE(sensor_colorfx_emboss_regs));
		break;
	case V4L2_COLORFX_SKETCH:
		ret = sensor_write_array(sd, sensor_colorfx_sketch_regs, ARRAY_SIZE(sensor_colorfx_sketch_regs));
		break;
	case V4L2_COLORFX_SKY_BLUE:
		ret = sensor_write_array(sd, sensor_colorfx_sky_blue_regs, ARRAY_SIZE(sensor_colorfx_sky_blue_regs));
		break;
	case V4L2_COLORFX_GRASS_GREEN:
		ret = sensor_write_array(sd, sensor_colorfx_grass_green_regs, ARRAY_SIZE(sensor_colorfx_grass_green_regs));
		break;
	case V4L2_COLORFX_SKIN_WHITEN:
		ret = sensor_write_array(sd, sensor_colorfx_skin_whiten_regs, ARRAY_SIZE(sensor_colorfx_skin_whiten_regs));
		break;
	case V4L2_COLORFX_VIVID:
		ret = sensor_write_array(sd, sensor_colorfx_vivid_regs, ARRAY_SIZE(sensor_colorfx_vivid_regs));
		break;
	default:
		return -EINVAL;
	}

	if (ret < 0) {
		csi_dev_err("sensor_s_colorfx error, return %x!\n",ret);
		return ret;
	}

	msleep(10);
	info->clrfx = value;
	return 0;
}

static int sensor_g_flash_mode(struct v4l2_subdev *sd,
    __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	enum v4l2_flash_mode *flash_mode = (enum v4l2_flash_mode*)value;

	*flash_mode = info->flash_mode;
	return 0;
}

static int sensor_s_flash_mode(struct v4l2_subdev *sd,
    enum v4l2_flash_mode value)
{
	struct sensor_info *info = to_state(sd);
	struct csi_dev *dev=(struct csi_dev *)dev_get_drvdata(sd->v4l2_dev->dev);
	char csi_flash_str[32];
	int flash_on,flash_off;

	if(info->ccm_info->iocfg == 0) {
		strcpy(csi_flash_str,"csi_flash");
	} else if(info->ccm_info->iocfg == 1) {
	  strcpy(csi_flash_str,"csi_flash_b");
	}

	flash_on = (dev->flash_pol!=0)?1:0;
	flash_off = (flash_on==1)?0:1;

	switch (value) {
	case V4L2_FLASH_MODE_OFF:
	  gpio_write_one_pin_value(dev->csi_pin_hd,flash_off,csi_flash_str);
		break;
	case V4L2_FLASH_MODE_AUTO:
		return -EINVAL;
		break;
	case V4L2_FLASH_MODE_ON:
		gpio_write_one_pin_value(dev->csi_pin_hd,flash_on,csi_flash_str);
		break;
	case V4L2_FLASH_MODE_TORCH:
		return -EINVAL;
		break;
	case V4L2_FLASH_MODE_RED_EYE:
		return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	info->flash_mode = value;
	return 0;
}

static int sensor_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return sensor_g_brightness(sd, &ctrl->value);
	case V4L2_CID_CONTRAST:
		return sensor_g_contrast(sd, &ctrl->value);
	case V4L2_CID_SATURATION:
		return sensor_g_saturation(sd, &ctrl->value);
	case V4L2_CID_HUE:
		return sensor_g_hue(sd, &ctrl->value);
	case V4L2_CID_VFLIP:
		return sensor_g_vflip(sd, &ctrl->value);
	case V4L2_CID_HFLIP:
		return sensor_g_hflip(sd, &ctrl->value);
	case V4L2_CID_GAIN:
		return sensor_g_gain(sd, &ctrl->value);
	case V4L2_CID_AUTOGAIN:
		return sensor_g_autogain(sd, &ctrl->value);
	case V4L2_CID_EXPOSURE:
		return sensor_g_exp(sd, &ctrl->value);
	case V4L2_CID_EXPOSURE_AUTO:
		return sensor_g_autoexp(sd, &ctrl->value);
	case V4L2_CID_DO_WHITE_BALANCE:
		return sensor_g_wb(sd, &ctrl->value);
	case V4L2_CID_AUTO_WHITE_BALANCE:
		return sensor_g_autowb(sd, &ctrl->value);
	case V4L2_CID_COLORFX:
		return sensor_g_colorfx(sd,	&ctrl->value);
	case V4L2_CID_CAMERA_FLASH_MODE:
		return sensor_g_flash_mode(sd, &ctrl->value);
	}
	return -EINVAL;
}

static int sensor_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return sensor_s_brightness(sd, ctrl->value);
	case V4L2_CID_CONTRAST:
		return sensor_s_contrast(sd, ctrl->value);
	case V4L2_CID_SATURATION:
		return sensor_s_saturation(sd, ctrl->value);
	case V4L2_CID_HUE:
		return sensor_s_hue(sd, ctrl->value);
	case V4L2_CID_VFLIP:
		return sensor_s_vflip(sd, ctrl->value);
	case V4L2_CID_HFLIP:
		return sensor_s_hflip(sd, ctrl->value);
	case V4L2_CID_GAIN:
		return sensor_s_gain(sd, ctrl->value);
	case V4L2_CID_AUTOGAIN:
		return sensor_s_autogain(sd, ctrl->value);
	case V4L2_CID_EXPOSURE:
		return sensor_s_exp(sd, ctrl->value);
	case V4L2_CID_EXPOSURE_AUTO:
		return sensor_s_autoexp(sd,
				(enum v4l2_exposure_auto_type) ctrl->value);
	case V4L2_CID_DO_WHITE_BALANCE:
		return sensor_s_wb(sd,
				(enum v4l2_whiteblance) ctrl->value);
	case V4L2_CID_AUTO_WHITE_BALANCE:
		return sensor_s_autowb(sd, ctrl->value);
	case V4L2_CID_COLORFX:
		return sensor_s_colorfx(sd,
				(enum v4l2_colorfx) ctrl->value);
	case V4L2_CID_CAMERA_FLASH_MODE:
	  return sensor_s_flash_mode(sd,
	      (enum v4l2_flash_mode) ctrl->value);
	}
	return -EINVAL;
}


static int sensor_g_chip_ident(struct v4l2_subdev *sd,
		struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SENSOR, 0);
}


/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops sensor_core_ops = {
	.g_chip_ident = sensor_g_chip_ident,
	.g_ctrl = sensor_g_ctrl,
	.s_ctrl = sensor_s_ctrl,
	.queryctrl = sensor_queryctrl,
	.reset = sensor_reset,
	.init = sensor_init,
	.s_power = sensor_power,
	.ioctl = sensor_ioctl,
};

static const struct v4l2_subdev_video_ops sensor_video_ops = {
	.enum_mbus_fmt = sensor_enum_fmt,//linux-3.0
	.try_mbus_fmt = sensor_try_fmt,//linux-3.0
	.s_mbus_fmt = sensor_s_fmt,//linux-3.0
	.s_parm = sensor_s_parm,//linux-3.0
	.g_parm = sensor_g_parm,//linux-3.0
};

static const struct v4l2_subdev_ops sensor_ops = {
	.core = &sensor_core_ops,
	.video = &sensor_video_ops,
};

/* ----------------------------------------------------------------------- */

static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct sensor_info *info;
//	int ret;

	info = kzalloc(sizeof(struct sensor_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;
	v4l2_i2c_subdev_init(sd, client, &sensor_ops);

	info->fmt = &sensor_formats[0];
	info->ccm_info = &ccm_info_con;

	info->brightness = 0;
	info->contrast = 0;
	info->saturation = 0;
	info->hue = 0;
	info->hflip = 0;
	info->vflip = 0;
	info->gain = 0;
	info->autogain = 1;
	info->exp = 0;
	info->autoexp = 0;
	info->autowb = 1;
	info->wb = 0;
	info->clrfx = 0;
//	info->clkrc = 1;	/* 30fps */

	return 0;
}


static int sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{ "mt9m113", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

//linux-3.0
static struct i2c_driver sensor_driver = {
	.driver = {
		.owner = THIS_MODULE,
	.name = "mt9m113",
	},
	.probe = sensor_probe,
	.remove = sensor_remove,
	.id_table = sensor_id,
};
static __init int init_sensor(void)
{
	return i2c_add_driver(&sensor_driver);
}

static __exit void exit_sensor(void)
{
  i2c_del_driver(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);

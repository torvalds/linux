/*
 * drivers/media/video/sun4i_csi/device/mt9d112.c
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
 * A V4L2 driver for Micron mt9d112 cameras.
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

MODULE_AUTHOR("raymonxiu");
MODULE_DESCRIPTION("A low-level driver for Micron mt9d112 sensors");
MODULE_LICENSE("GPL");

//for internel driver debug
#define DEV_DBG_EN   		0
#if(DEV_DBG_EN == 1)
#define csi_dev_dbg(x,arg...) printk(KERN_INFO"[CSI_DEBUG][MT9D112]"x,##arg)
#else
#define csi_dev_dbg(x,arg...)
#endif
#define csi_dev_err(x,arg...) printk(KERN_INFO"[CSI_ERR][MT9D112]"x,##arg)
#define csi_dev_print(x,arg...) printk(KERN_INFO"[CSI][MT9D112]"x,##arg)

#define MCLK (24*1000*1000)
//#define MCLK (49.5*1000*1000)

#define VREF_POL	CSI_HIGH
#define HREF_POL	CSI_HIGH
#define CLK_POL		CSI_FALLING
#define IO_CFG		0						//0 for csi0

//define the voltage level of control signal
#define CSI_STBY_ON			1
#define CSI_STBY_OFF 		0
#define CSI_RST_ON			0
#define CSI_RST_OFF			1
#define CSI_PWR_ON			1
#define CSI_PWR_OFF			0


#define V4L2_IDENT_SENSOR 0x1320

#define REG_TERM 0xff
#define VAL_TERM 0xff


#define REG_ADDR_STEP 2
#define REG_DATA_STEP 2
#define REG_STEP 			(REG_ADDR_STEP+REG_DATA_STEP)


/*
 * Basic window sizes.  These probably belong somewhere more globally
 * useful.
 */
#define UXGA_WIDTH	1600
#define UXGA_HEIGHT	1200
#define VGA_WIDTH	640
#define VGA_HEIGHT	480
#define QVGA_WIDTH	320
#define QVGA_HEIGHT	240
#define CIF_WIDTH		352
#define CIF_HEIGHT	288
#define QCIF_WIDTH	176
#define	QCIF_HEIGHT	144

/*
 * Our nominal (default) frame rate.
 */
#define SENSOR_FRAME_RATE 25

/*
 * The Micron mt9d112 sits on i2c with ID 0x20
 */
#define I2C_ADDR 0x78//(0x78 for write,0x79 for read)

/* Registers */


/*
 * Information we maintain about a known sensor.
 */
struct sensor_format_struct;  /* coming later */
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


struct regval_list {
	unsigned char reg_num[REG_ADDR_STEP];
	unsigned char value[REG_DATA_STEP];
};



static struct regval_list sensor_default_regs[] = {
	{{0x33,0x86}, {0x25,0x01}}, 		//MCU_BOOT_MODE
	{{0x33,0x86}, {0x25,0x00}}, 		//MCU_BOOT_MODE
	{{0xff,0xff}, {0x00,0x64}},//DELAY= 100
	{{0x30,0x1A}, {0x0A,0xCC}}, 		//RESET_REGISTER
	{{0x32,0x02}, {0x00,0x08}}, 		//STANDBY_CONTROL
	{{0xff,0xff}, {0x00,0x64}},//DELAY = 100
	//{{0x32,0x14},{0x00,0x80}},
	{{0x33,0x8C}, {0xA2,0x15}}, 	 //AE maxADChi
	{{0x33,0x90}, {0x00,0x06}}, 	 //gain_thd , by jiujian
	{{0x33,0x8C}, {0xA2,0x06}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x36}}, // AE_TARGET
	{{0x33,0x8C}, {0xA2,0x07}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x04}}, // AE_GATE
	{{0x33,0x8C}, {0xA2,0x0c}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x08}}, // AE_GAT

	{{0x32,0x78}, {0x00,0x50}}, // first black level
	{{0x32,0x7a}, {0x00,0x50}}, // first black level,red
	{{0x32,0x7c}, {0x00,0x50}}, // green_1
	{{0x32,0x7e}, {0x00,0x50}}, // green_2
	{{0x32,0x80}, {0x00,0x50}}, // blue
	{{0xff,0xff}, {0x00,0x0a}},//DELAY = 10
	{{0x33,0x7e}, {0x20,0x00}}, // Y/RGB offset
	{{0x33,0x8C}, {0xA3,0x4A}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x59}}, 		// AWB_GAIN_MIN
	{{0x33,0x8C}, {0xA3,0x4B}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0xA6}}, 		// AWB_GAIN_MAX
	{{0x33,0x8C}, {0x23,0x5F}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x40}}, 		// AWB_CNT_PXL_TH
	{{0x33,0x8C}, {0xA3,0x61}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0xD2}}, 		// AWB_TG_MIN0
	{{0x33,0x8C}, {0xA3,0x62}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0xE6}}, 		// AWB_TG_MAX0
	{{0x33,0x8C}, {0xA3,0x63}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x10}}, 		// AWB_X0
	{{0x33,0x8C}, {0xA3,0x64}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0xA0}}, 		// AWB_KR_L
	{{0x33,0x8C}, {0xA3,0x65}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x96}}, 		// AWB_KG_L
	{{0x33,0x8C}, {0xA3,0x66}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x80}}, 		// AWB_KB_L
	{{0x33,0x8C}, {0xA3,0x67}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x80}}, 		// AWB_KR_R
	{{0x33,0x8C}, {0xA3,0x68}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x80}}, 		// AWB_KG_R
	{{0x33,0x8C}, {0xA3,0x69}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x80}}, 		// AWB_KB_R
	{{0x32,0xA2}, {0x36,0x40}}, 		// RESERVED_SOC1_32A2  //fine tune color setting
	{{0x33,0x8C}, {0x23,0x06}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x02,0xFF}}, 		// AWB_CCM_L_0
	{{0x33,0x8C}, {0x23,0x08}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFE,0x6E}}, 		// AWB_CCM_L_1
	{{0x33,0x8C}, {0x23,0x0A}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFF,0xC2}}, 		// AWB_CCM_L_2
	{{0x33,0x8C}, {0x23,0x0C}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFF,0x4A}}, 		// AWB_CCM_L_3
	{{0x33,0x8C}, {0x23,0x0E}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x02,0xD7}}, 		// AWB_CCM_L_4
	{{0x33,0x8C}, {0x23,0x10}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFF,0x30}}, 		// AWB_CCM_L_5
	{{0x33,0x8C}, {0x23,0x12}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFF,0x6E}}, 		// AWB_CCM_L_6
	{{0x33,0x8C}, {0x23,0x14}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFD,0xEE}}, 		// AWB_CCM_L_7
	{{0x33,0x8C}, {0x23,0x16}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x03,0xCF}}, 		// AWB_CCM_L_8
	{{0x33,0x8C}, {0x23,0x18}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x20}}, 		// AWB_CCM_L_9
	{{0x33,0x8C}, {0x23,0x1A}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x3C}}, 		// AWB_CCM_L_10
	{{0x33,0x8C}, {0x23,0x1C}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x2C}}, 		// AWB_CCM_RL_0
	{{0x33,0x8C}, {0x23,0x1E}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFF,0xBC}}, 		// AWB_CCM_RL_1
	{{0x33,0x8C}, {0x23,0x20}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x16}}, 		// AWB_CCM_RL_2
	{{0x33,0x8C}, {0x23,0x22}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x37}}, 		// AWB_CCM_RL_3
	{{0x33,0x8C}, {0x23,0x24}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFF,0xCD}}, 		// AWB_CCM_RL_4
	{{0x33,0x8C}, {0x23,0x26}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFF,0xF3}}, 		// AWB_CCM_RL_5
	{{0x33,0x8C}, {0x23,0x28}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x77}}, 		// AWB_CCM_RL_6
	{{0x33,0x8C}, {0x23,0x2A}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0xF4}}, 		// AWB_CCM_RL_7
	{{0x33,0x8C}, {0x23,0x2C}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFE,0x95}}, 		// AWB_CCM_RL_8
	{{0x33,0x8C}, {0x23,0x2E}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x14}}, 		// AWB_CCM_RL_9
	{{0x33,0x8C}, {0x23,0x30}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0xFF,0xE8}}, 		// AWB_CCM_RL_10  //end
	{{0x33,0x8C}, {0xA3,0x48}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x08}}, 		// AWB_GAIN_BUFFER_SPEED
	{{0x33,0x8C}, {0xA3,0x49}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x02}}, 		// AWB_JUMP_DIVISOR
	{{0x33,0x8C}, {0xA3,0x4A}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x59}}, 		// AWB_GAIN_MIN
	{{0x33,0x8C}, {0xA3,0x4B}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0xA6}}, 		// AWB_GAIN_MAX
	{{0x33,0x8C}, {0xA3,0x4F}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x00}}, 		// AWB_CCM_POSITION_MIN
	{{0x33,0x8C}, {0xA3,0x50}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x7F}}, 		// AWB_CCM_POSITION_MAX
	{{0x33,0x8C}, {0xA3,0x52}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x1E}}, 		// AWB_SATURATION
	{{0x33,0x8C}, {0xA3,0x53}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x02}}, 		// AWB_MODE
	{{0x33,0x8C}, {0xA3,0x5B}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x7E}}, 		// AWB_STEADY_BGAIN_OUT_MIN
	{{0x33,0x8C}, {0xA3,0x5C}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x86}}, 		// AWB_STEADY_BGAIN_OUT_MAX
	{{0x33,0x8C}, {0xA3,0x5D}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x7F}}, 		// AWB_STEADY_BGAIN_IN_MIN
	{{0x33,0x8C}, {0xA3,0x5E}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x82}}, 		// AWB_STEADY_BGAIN_IN_MAX
	{{0x33,0x8C}, {0x23,0x5F}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x40}}, 		// AWB_CNT_PXL_TH
	{{0x33,0x8C}, {0xA3,0x61}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0xD2}}, 		// AWB_TG_MIN0
	{{0x33,0x8C}, {0xA3,0x62}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0xE6}}, 		// AWB_TG_MAX0
	{{0x33,0x8C}, {0xA3,0x02}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x00}}, 		// AWB_WINDOW_POS
	{{0x33,0x8C}, {0xA3,0x03}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0xEF}}, 		// AWB_WINDOW_SIZE
	{{0x33,0x8C}, {0xAB,0x05}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x00}}, 		// HG_PERCENT
	{{0x33,0x8C}, {0xA7,0x82}}, 		// MCU_ADDRESS
	{{0x35,0xA4}, {0x05,0x96}}, 		// BRIGHT_COLOR_KILL_CONTROLS
	{{0x33,0x8C}, {0xA1,0x18}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x1E}}, 		// SEQ_LLSAT1
	{{0x33,0x8C}, {0xA1,0x19}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x04}}, 		// SEQ_LLSAT2
	{{0x33,0x8C}, {0xA1,0x1A}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x0A}}, 		// SEQ_LLINTERPTHRESH1
	{{0x33,0x8C}, {0xA1,0x1B}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x20}}, 		// SEQ_LLINTERPTHRESH2
	{{0x33,0x8C}, {0xA1,0x3E}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x04}}, 		// SEQ_NR_TH1_R
	{{0x33,0x8C}, {0xA1,0x3F}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x0E}}, 		// SEQ_NR_TH1_G
	{{0x33,0x8C}, {0xA1,0x40}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x04}}, 		// SEQ_NR_TH1_B
	{{0x33,0x8C}, {0xA1,0x41}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x04}}, 		// SEQ_NR_TH1_OL
	{{0x33,0x8C}, {0xA1,0x42}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x32}}, 		// SEQ_NR_TH2_R
	{{0x33,0x8C}, {0xA1,0x43}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x0F}}, 		// SEQ_NR_TH2_G
	{{0x33,0x8C}, {0xA1,0x44}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x32}}, 		// SEQ_NR_TH2_B
	{{0x33,0x8C}, {0xA1,0x45}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x32}}, 		// SEQ_NR_TH2_OL
	{{0x33,0x8C}, {0xA1,0x46}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x05}}, 		// SEQ_NR_GAINTH1
	{{0x33,0x8C}, {0xA1,0x47}}, 		// MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x3A}}, 		// SEQ_NR_GAINTH2

	{{0x33, 0x8C}, {0x27, 0x03}},		//Output Width (A)
	{{0x33, 0x90}, {0x02, 0x80}},		 // 	 = 640
	{{0x33, 0x8C}, {0x27, 0x05}},		 //Output Height (A)
	{{0x33, 0x90}, {0x01, 0xE0}},		 // 	 = 480
	{{0x33, 0x8C}, {0x27, 0x07}},		 //Output Width (B)
	{{0x33, 0x90}, {0x06, 0x40}},		 // 	 = 1600
	{{0x33, 0x8C}, {0x27, 0x09}},		//Output Height (B)
	{{0x33, 0x90}, {0x04, 0xB0}},		 // 	 = 1200
	{{0x33, 0x8C}, {0x27, 0x0D}},		 //Row Start (A)
	{{0x33, 0x90}, {0x00, 0x78}},		 // 	 = 120
	{{0x33, 0x8C}, {0x27, 0x0F}},		 //Column Start (A)
	{{0x33, 0x90}, {0x00, 0xA0}},		 // 	 = 160
	{{0x33, 0x8C}, {0x27, 0x11}},		 //Row End (A)
	{{0x33, 0x90}, {0x04, 0x4d}},		 // 	 = 1101
	{{0x33, 0x8C}, {0x27, 0x13}},		 //Column End (A)
	{{0x33, 0x90}, {0x05, 0xb5}},		 // 	 = 1461
	{{0x33, 0x8C}, {0x27, 0x15}},		 //Extra Delay (A)
	{{0x33, 0x90}, {0x00, 0xAF}},		 // 	 = 175
	{{0x33, 0x8C}, {0x27, 0x17}},		 //Row Speed (A)
	{{0x33, 0x90}, {0x21, 0x11}},		 // 	 = 8465
	{{0x33, 0x8C}, {0x27, 0x19}},		 //Read Mode (A)
	{{0x33, 0x90}, {0x04, 0x6c}},		 // 	 = 1132
	{{0x33, 0x8C}, {0x27, 0x1B}},		 //sensor_sample_time_pck (A)
	{{0x33, 0x90}, {0x02, 0x4F}},		 // 	 = 591
	{{0x33, 0x8C}, {0x27, 0x1D}},		 //sensor_fine_correction (A)
	{{0x33, 0x90}, {0x01, 0x02}},		 // 	 = 258
	{{0x33, 0x8C}, {0x27, 0x1F}},		 //sensor_fine_IT_min (A)
	{{0x33, 0x90}, {0x02, 0x79}},		 // 	 = 633
	{{0x33, 0x8C}, {0x27, 0x21}},		 //sensor_fine_IT_max_margin (A)
	{{0x33, 0x90}, {0x01, 0x55}},		 // 	 = 341
	{{0x33, 0x8C}, {0x27, 0x23}},		 //Frame Lines (A)
	{{0x33, 0x90}, {0x02, 0x05}},		 // 	 = 575
	{{0x33, 0x8C}, {0x27, 0x25}},		 //Line Length (A)
	{{0x33, 0x90}, {0x05, 0x6F}},		 // 	 = 1391
	{{0x33, 0x8C}, {0x27, 0x27}},		 //sensor_dac_id_4_5 (A)
	{{0x33, 0x90}, {0x20, 0x20}},		 // 	 = 8224
	{{0x33, 0x8C}, {0x27, 0x29}},		 //sensor_dac_id_6_7 (A)
	{{0x33, 0x90}, {0x20, 0x20}},		 // 	 = 8224
	{{0x33, 0x8C}, {0x27, 0x2B}},		 //sensor_dac_id_8_9 (A)
	{{0x33, 0x90}, {0x10, 0x20}},		 // 	 = 4128
	{{0x33, 0x8C}, {0x27, 0x2D}},		 //sensor_dac_id_10_11 (A)
	{{0x33, 0x90}, {0x20, 0x07}},		 // 	 = 8199
	{{0x33, 0x8c}, {0x27, 0x95}},		   // Natural , Swaps chrominance byte
	{{0x33, 0x90}, {0x00, 0x02}},
	{{0x33, 0x8C}, {0x27, 0x2F}},		 //Row Start (B)
	{{0x33, 0x90}, {0x00, 0x04}},		 // 	 = 4
	{{0x33, 0x8C}, {0x27, 0x31}},		 //Column Start (B)
	{{0x33, 0x90}, {0x00, 0x04}},		 // 	 = 4
	{{0x33, 0x8C}, {0x27, 0x33}},		 //Row End (B)
	{{0x33, 0x90}, {0x04, 0xBB}},		 // 	 = 1211
	{{0x33, 0x8C}, {0x27, 0x35}},		 //Column End (B)
	{{0x33, 0x90}, {0x06, 0x4B}},		 // 	 = 1611
	{{0x33, 0x8C}, {0x27, 0x37}},		 //Extra Delay (B)
	{{0x33, 0x90}, {0x00, 0x7C}},		 // 	 = 124
	{{0x33, 0x8C}, {0x27, 0x39}},		 //Row Speed (B)
	{{0x33, 0x90}, {0x21, 0x11}},		 // 	 = 8465
	{{0x33, 0x8C}, {0x27, 0x3B}},		 //Read Mode (B)
	{{0x33, 0x90}, {0x00, 0x24}},		 // 	 = 36
	{{0x33, 0x8C}, {0x27, 0x3D}},		 //sensor_sample_time_pck (B)
	{{0x33, 0x90}, {0x01, 0x20}},		 // 	 = 288
	{{0x33, 0x8C}, {0x27, 0x41}},		 //sensor_fine_IT_min (B)
	{{0x33, 0x90}, {0x01, 0x69}},		 // 	 = 361
	{{0x33, 0x8C}, {0x27, 0x45}},		 //Frame Lines (B)
	{{0x33, 0x90}, {0x04, 0xFC}},		 // 	 = 1276
	{{0x33, 0x8C}, {0x27, 0x47}},		 //Line Length (B)
	{{0x33, 0x90}, {0x09, 0x2F}},		 // 	 = 2351
	{{0x33, 0x8C}, {0x27, 0x51}},		 //Crop_X0 (A)
	{{0x33, 0x90}, {0x00, 0x00}},		 // 	 = 0
	{{0x33, 0x8C}, {0x27, 0x53}},		 //Crop_X1 (A)
	{{0x33, 0x90}, {0x02, 0x80}},		 // 	 = 640
	{{0x33, 0x8C}, {0x27, 0x55}},		 //Crop_Y0 (A)
	{{0x33, 0x90}, {0x00, 0x00}},		 // 	 = 0
	{{0x33, 0x8C}, {0x27, 0x57}},		 //Crop_Y1 (A)
	{{0x33, 0x90}, {0x01, 0xE0}},		 // 	 = 480
	{{0x33, 0x8C}, {0x27, 0x5F}},		 //Crop_X0 (B)
	{{0x33, 0x90}, {0x00, 0x00}},		 // 	 = 0
	{{0x33, 0x8C}, {0x27, 0x61}},		 //Crop_X1 (B)
	{{0x33, 0x90}, {0x06, 0x40}},		 // 	 = 1600
	{{0x33, 0x8C}, {0x27, 0x63}},		 //Crop_Y0 (B)
	{{0x33, 0x90}, {0x00, 0x00}},		 // 	 = 0
	{{0x33, 0x8C}, {0x27, 0x65}},		 //Crop_Y1 (B)
	{{0x33, 0x90}, {0x04, 0xB0}},		 // 	 = 1200
	{{0x33, 0x8C}, {0x22, 0x2E}},		 //R9 Step
	{{0x33, 0x90}, {0x00, 0x90}},		 // 	 = 144
	{{0x33, 0x8C}, {0xA4, 0x08}},		 //search_f1_50
	{{0x33, 0x90}, {0x00, 0x1A}},		 // 	 = 26
	{{0x33, 0x8C}, {0xA4, 0x09}},		 //search_f2_50
	{{0x33, 0x90}, {0x00, 0x1D}},		 // 	 = 29
	{{0x33, 0x8C}, {0xA4, 0x0A}},		 //search_f1_60
	{{0x33, 0x90}, {0x00, 0x20}},		 // 	 = 32
	{{0x33, 0x8C}, {0xA4, 0x0B}},		 //search_f2_60
	{{0x33, 0x90}, {0x00, 0x23}},		 // 	 = 35
	{{0x33, 0x8C}, {0x24, 0x11}},		 //R9_Step_60_A
	{{0x33, 0x90}, {0x00, 0x90}},		 // 	 = 144
	{{0x33, 0x8C}, {0x24, 0x13}},		 //R9_Step_50_A
	{{0x33, 0x90}, {0x00, 0xAD}},		 // 	 = 173
	{{0x33, 0x8C}, {0x24, 0x15}},		 //R9_Step_60_B
	{{0x33, 0x90}, {0x00, 0x55}},		 // 	 = 85
	{{0x33, 0x8C}, {0x24, 0x17}},		 //R9_Step_50_B
	{{0x33, 0x90}, {0x00, 0x66}},		 // 	 = 102

	{{0x33, 0x8C}, {0xA3, 0x4A}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x59}},		// AWB_GAIN_MIN
	{{0x33, 0x8C}, {0xA3, 0x4B}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0xA6}},		// AWB_GAIN_MAX
	{{0x33, 0x8C}, {0x23, 0x5F}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x40}},		// AWB_CNT_PXL_TH
	{{0x33, 0x8C}, {0xA3, 0x61}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0xD2}},		// AWB_TG_MIN0
	{{0x33, 0x8C}, {0xA3, 0x62}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0xE6}},		// AWB_TG_MAX0
	{{0x33, 0x8C}, {0xA3, 0x63}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x10}},		// AWB_X0
	{{0x33, 0x8C}, {0xA3, 0x64}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0xA0}},		// AWB_KR_L
	{{0x33, 0x8C}, {0xA3, 0x65}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x96}},		// AWB_KG_L
	{{0x33, 0x8C}, {0xA3, 0x66}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x80}},		// AWB_KB_L
	{{0x33, 0x8C}, {0xA3, 0x67}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x80}},		// AWB_KR_R
	{{0x33, 0x8C}, {0xA3, 0x68}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x80}},		// AWB_KG_R
	{{0x33, 0x8C}, {0xA3, 0x69}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x80}},		// AWB_KB_R

	{{0x32, 0xA2}, {0x36, 0x40}},		// RESERVED_SOC1_32A2  //fine tune color setting
	{{0x33, 0x8C}, {0x23, 0x06}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x02, 0xFF}},		// AWB_CCM_L_0
	{{0x33, 0x8C}, {0x23, 0x08}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFE, 0x6E}},		// AWB_CCM_L_1
	{{0x33, 0x8C}, {0x23, 0x0A}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFF, 0xC2}},		// AWB_CCM_L_2
	{{0x33, 0x8C}, {0x23, 0x0C}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFF, 0x4A}},		// AWB_CCM_L_3
	{{0x33, 0x8C}, {0x23, 0x0E}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x02, 0xD7}},		// AWB_CCM_L_4
	{{0x33, 0x8C}, {0x23, 0x10}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFF, 0x30}},		// AWB_CCM_L_5
	{{0x33, 0x8C}, {0x23, 0x12}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFF, 0x6E}},		// AWB_CCM_L_6
	{{0x33, 0x8C}, {0x23, 0x14}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFD, 0xEE}},		// AWB_CCM_L_7
	{{0x33, 0x8C}, {0x23, 0x16}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x03, 0xCF}},		// AWB_CCM_L_8
	{{0x33, 0x8C}, {0x23, 0x18}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x20}},		// AWB_CCM_L_9
	{{0x33, 0x8C}, {0x23, 0x1A}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x3C}},		// AWB_CCM_L_10
	{{0x33, 0x8C}, {0x23, 0x1C}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x2C}},		// AWB_CCM_RL_0
	{{0x33, 0x8C}, {0x23, 0x1E}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFF, 0xBC}},		// AWB_CCM_RL_1
	{{0x33, 0x8C}, {0x23, 0x20}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x16}},		// AWB_CCM_RL_2
	{{0x33, 0x8C}, {0x23, 0x22}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x37}},		// AWB_CCM_RL_3
	{{0x33, 0x8C}, {0x23, 0x24}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFF, 0xCD}},		// AWB_CCM_RL_4
	{{0x33, 0x8C}, {0x23, 0x26}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFF, 0xF3}},		// AWB_CCM_RL_5
	{{0x33, 0x8C}, {0x23, 0x28}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x77}},		// AWB_CCM_RL_6
	{{0x33, 0x8C}, {0x23, 0x2A}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0xF4}},		// AWB_CCM_RL_7
	{{0x33, 0x8C}, {0x23, 0x2C}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFE, 0x95}},		// AWB_CCM_RL_8
	{{0x33, 0x8C}, {0x23, 0x2E}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x14}},		// AWB_CCM_RL_9
	{{0x33, 0x8C}, {0x23, 0x30}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0xFF, 0xE8}},		// AWB_CCM_RL_10
	//end
	{{0x33, 0x8C}, {0xA3, 0x48}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x08}},		// AWB_GAIN_BUFFER_SPEED
	{{0x33, 0x8C}, {0xA3, 0x49}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x02}},		// AWB_JUMP_DIVISOR
	{{0x33, 0x8C}, {0xA3, 0x4A}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x59}},		// AWB_GAIN_MIN
	{{0x33, 0x8C}, {0xA3, 0x4B}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0xA6}},		// AWB_GAIN_MAX
	{{0x33, 0x8C}, {0xA3, 0x4F}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x00}},		// AWB_CCM_POSITION_MIN
	{{0x33, 0x8C}, {0xA3, 0x50}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x7F}},		// AWB_CCM_POSITION_MAX
	{{0x33, 0x8C}, {0xA3, 0x52}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x1E}},		// AWB_SATURATION
	{{0x33, 0x8C}, {0xA3, 0x53}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x02}},		// AWB_MODE
	{{0x33, 0x8C}, {0xA3, 0x5B}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x7E}},		// AWB_STEADY_BGAIN_OUT_MIN
	{{0x33, 0x8C}, {0xA3, 0x5C}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x86}},		// AWB_STEADY_BGAIN_OUT_MAX
	{{0x33, 0x8C}, {0xA3, 0x5D}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x7F}},		// AWB_STEADY_BGAIN_IN_MIN
	{{0x33, 0x8C}, {0xA3, 0x5E}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x82}},		// AWB_STEADY_BGAIN_IN_MAX
	{{0x33, 0x8C}, {0x23, 0x5F}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x40}},		// AWB_CNT_PXL_TH
	{{0x33, 0x8C}, {0xA3, 0x61}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0xD2}},		// AWB_TG_MIN0
	{{0x33, 0x8C}, {0xA3, 0x62}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0xE6}},		// AWB_TG_MAX0
	{{0x33, 0x8C}, {0xA3, 0x02}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x00}},		// AWB_WINDOW_POS
	{{0x33, 0x8C}, {0xA3, 0x03}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0xEF}},		// AWB_WINDOW_SIZE
	{{0x33, 0x8C}, {0xAB, 0x05}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x00}},		// HG_PERCENT
	{{0x33, 0x8C}, {0xA7, 0x82}},		// MCU_ADDRESS
	{{0x35, 0xA4}, {0x05, 0x96}},		// BRIGHT_COLOR_KILL_CONTROLS
	{{0x33, 0x8C}, {0xA1, 0x18}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x1E}},		// SEQ_LLSAT1
	{{0x33, 0x8C}, {0xA1, 0x19}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x04}},		// SEQ_LLSAT2
	{{0x33, 0x8C}, {0xA1, 0x1A}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x0A}},		// SEQ_LLINTERPTHRESH1
	{{0x33, 0x8C}, {0xA1, 0x1B}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x20}},		// SEQ_LLINTERPTHRESH2
	{{0x33, 0x8C}, {0xA1, 0x3E}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x04}},		// SEQ_NR_TH1_R
	{{0x33, 0x8C}, {0xA1, 0x3F}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x0E}},		// SEQ_NR_TH1_G
	{{0x33, 0x8C}, {0xA1, 0x40}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x04}},		// SEQ_NR_TH1_B
	{{0x33, 0x8C}, {0xA1, 0x41}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x04}},		// SEQ_NR_TH1_OL
	{{0x33, 0x8C}, {0xA1, 0x42}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x32}},		// SEQ_NR_TH2_R
	{{0x33, 0x8C}, {0xA1, 0x43}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x0F}},		// SEQ_NR_TH2_G
	{{0x33, 0x8C}, {0xA1, 0x44}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x32}},		// SEQ_NR_TH2_B
	{{0x33, 0x8C}, {0xA1, 0x45}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x32}},		// SEQ_NR_TH2_OL
	{{0x33, 0x8C}, {0xA1, 0x46}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x05}},		// SEQ_NR_GAINTH1
	{{0x33, 0x8C}, {0xA1, 0x47}},		// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x3A}},		// SEQ_NR_GAINTH2
	{{0x33, 0x8C}, {0xA2, 0x15}},	   //AE maxADChi
	{{0x33, 0x90}, {0x00, 0x06}},	   //gain_thd , by ethan

	{{0x33,0x8c},  {0xa2,0x07}},// AE sensitivity
	{{0x33,0x90},  {0x00,0x40}},

	{{0x33, 0x8C}, {0xA4, 0x0D}},		 //Stat_min
	{{0x33, 0x90}, {0x00, 0x02}},		 // 	 = 2
	{{0x33, 0x8C}, {0xA4, 0x10}},		 //Min_amplitude
	{{0x33, 0x90}, {0x00, 0x01}},		 // 	 = 1
	{{0x33,0x8C},{0x27,0x5F}}, 	// MCU_ADDRESS [MODE_CROP_X0_B]
	{{0x33,0x90},{0x00,0x00}}, 	// MCU_DATA_0
	{{0x33,0x8C},{0x27,0x61}}, 	// MCU_ADDRESS [MODE_CROP_X1_B]
	{{0x33,0x90},{0x06,0x40}}, 	// MCU_DATA_0
	{{0x33,0x8C},{0x27,0x63}}, 	// MCU_ADDRESS [MODE_CROP_Y0_B]
	{{0x33,0x90},{0x00,0x00}}, 	// MCU_DATA_0
	{{0x33,0x8C},{0x27,0x65}}, 	// MCU_ADDRESS [MODE_CROP_Y1_B]
	{{0x33,0x90},{0x04,0xB0}}, 	// MCU_DATA_0
};

static struct regval_list sensor_uxga_regs[] = {
  {{0xff,0xff},  {0x00,0x0a}},           //delay 10ms
  {{0x34, 0x1E}, {0x8F, 0x09}},		 //PLL/ Clk_in control: BYPASS PLL = 0x8F09
	{{0x34, 0x1C}, {0x01, 0x20}},		 //PLL Control 1 = 0x120
	{{0xff,0xff},  {0x00,0x0a}},           //delay 10ms
	{{0x34, 0x1E}, {0x8F, 0x09}},		 //PLL/ Clk_in control: PLL ON, bypassed = 0x8F09
	{{0x34, 0x1E}, {0x8F, 0x08}},		 //PLL/ Clk_in control: USE PLL = 0x8F08

	{{0x33,0x8c},{0xa1,0x03}},
	{{0x33,0x90},{0x00,0x02}},
	{{0xff,0xff},{0x00,0x0a}},			//delay 10ms

  {{0x33,0x8c},{0xa1,0x20}},
	{{0x33,0x90},{0x00,0x02}},
	{{0x33,0x8c},{0xa1,0x03}},
	{{0x33,0x90},{0x00,0x02}},
	{{0xff,0xff},{0x00,0x64}},
	{{0x33,0x8c},{0xa1,0x20}},
	{{0x33,0x90},{0x00,0x02}},
	{{0x33,0x8c},{0xa1,0x03}},
	{{0x33,0x90},{0x00,0x02}},
	{{0xff,0xff},{0x00,0x64}},
	{{0x33,0x8c},{0xa1,0x03}},
	{{0x33,0x90},{0x00,0x02}},
	{{0x33,0x8c},{0xa1,0x03}},
	{{0x33,0x90},{0x00,0x02}},
	{{0xff,0xff},{0x00,0xff}},
};

static struct regval_list sensor_vga_regs[] = {

	{{0x30,0x1A},  {0x0A,0xCC}},	 // RESET_REGISTER
  {{0x32,0x02},	 { 0x00,0x08}},	 // STANDBY_CONTROL
  {{0xff,0xff},  {0x00,0x10}},       //delay 10ms
	{{0x34, 0x1E}, {0x8F, 0x09}},		 //PLL/ Clk_in control: BYPASS PLL = 0x8F09
	{{0x34, 0x1C}, {0x01, 0x20}},		 //PLL Control 1 = 0x120
	{{0xff,0xff},  {0x00,0x0a}},       //delay 10ms
	{{0x34, 0x1E}, {0x8F, 0x09}},		 //PLL/ Clk_in control: PLL ON, bypassed = 0x8F09
	{{0x34, 0x1E}, {0x8F, 0x08}},		 //PLL/ Clk_in control: USE PLL = 0x8F08

	{{0x33, 0x8C}, {0xA4, 0x0D}},		 //Stat_min
	{{0x33, 0x90}, {0x00, 0x02}},		 // 	 = 2
	{{0x33, 0x8C}, {0xA4, 0x10}},		 //Min_amplitude
	{{0x33, 0x90}, {0x00, 0x01}},		 // 	 = 1
	{{0x33, 0x8C}, {0xA1, 0x03}},		 //Refresh Sequencer Mode
	{{0x33, 0x90}, {0x00, 0x06}},		 // 	 = 6
	{{0xff,0xff}, {0x00,0x64}},				//delay 100ms
	{{0x33, 0x8C}, {0xA1, 0x03}},		 //Refresh Sequencer
	{{0x33, 0x90}, {0x00, 0x05}},		 // 	 = 5
	{{0xff,0xff}, {0x00,0x64}},				//delay 100ms

	{{0x33, 0xf4}, {0x03, 0x1d}},		//defect
	{{0x33, 0x8c}, {0xa1, 0x18}},	  //saturation
	{{0x33, 0x90}, {0x00, 0x26}},

	{{0x33, 0x8C}, {0xA1, 0x20}},	// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x00}},	// SEQ_CAP_MODE
	{{0x33, 0x8C}, {0xA1, 0x03}},	// MCU_ADDRESS
	{{0x33, 0x90}, {0x00, 0x01}},	// SEQ_CM
	{{0xff,0xff}, {0x00,0xff}},		//delay 100ms
};

/*
 * The white balance settings
 * Here only tune the R G B channel gain.
 * The white balance enalbe bit is modified in sensor_s_autowb and sensor_s_wb
 */
static struct regval_list sensor_wb_auto_regs[] = {
{{0x33,0x8C}, {0xA1,0x02}},
{{0x33,0x90}, {0x00,0x0F}},
{{0x33,0x8C}, {0xA3,0x67}},
{{0x33,0x90}, {0x00,0x80}},
{{0x33,0x8C}, {0xA3,0x68}},
{{0x33,0x90}, {0x00,0x80}},
{{0x33,0x8C}, {0xA3,0x69}},
{{0x33,0x90}, {0x00,0x80}},
};

static struct regval_list sensor_wb_cloud_regs[] = {
{{0x33,0x8C}, {0xA1,0x02}},
{{0x33,0x90}, {0x00,0x0B}},
{{0x33,0x8C}, {0xA3,0x67}},
{{0x33,0x90}, {0x00,0x9f}},
{{0x33,0x8C}, {0xA3,0x68}},
{{0x33,0x90}, {0x00,0x80}},
{{0x33,0x8C}, {0xA3,0x69}},
{{0x33,0x90}, {0x00,0x7e}},
};

static struct regval_list sensor_wb_daylight_regs[] = {
	//tai yang guang
{{0x33,0x8C}, {0xA1,0x02}},
{{0x33,0x90}, {0x00,0x0B}},
{{0x33,0x8C}, {0xA3,0x67}},
{{0x33,0x90}, {0x00,0x85}},
{{0x33,0x8C}, {0xA3,0x68}},
{{0x33,0x90}, {0x00,0x90}},
{{0x33,0x8C}, {0xA3,0x69}},
{{0x33,0x90}, {0x00,0x90}},
};

static struct regval_list sensor_wb_incandescence_regs[] = {
	//bai re guang
{{0x33,0x8C}, {0xA1,0x02}},
{{0x33,0x90}, {0x00,0x0B}},

{{0x33,0x8C}, {0xA3,0x67}},
{{0x33,0x90}, {0x00,0x81}},
{{0x33,0x8C}, {0xA3,0x68}},
{{0x33,0x90}, {0x00,0x88}},
{{0x33,0x8C}, {0xA3,0x69}},
{{0x33,0x90}, {0x00,0x83}},
};

static struct regval_list sensor_wb_fluorescent_regs[] = {
	//ri guang deng
{{0x33,0x8C}, {0xA1,0x02}},
{{0x33,0x90}, {0x00,0x0B}},

{{0x33,0x8C}, {0xA3,0x67}},
{{0x33,0x90}, {0x00,0x81}},
{{0x33,0x8C}, {0xA3,0x68}},
{{0x33,0x90}, {0x00,0x80}},
{{0x33,0x8C}, {0xA3,0x69}},
{{0x33,0x90}, {0x00,0x98}},
};

static struct regval_list sensor_wb_tungsten_regs[] = {
	//wu si deng
{{0x33,0x8C}, {0xA1,0x02}},
{{0x33,0x90}, {0x00,0x0B}},

{{0x33,0x8C}, {0xA3,0x67}},
{{0x33,0x90}, {0x00,0x91}},
{{0x33,0x8C}, {0xA3,0x68}},
{{0x33,0x90}, {0x00,0x7F}},
{{0x33,0x8C}, {0xA3,0x69}},
{{0x33,0x90}, {0x00,0x82}},
};

/*
 * The color effect settings
 */
static struct regval_list sensor_colorfx_none_regs[] = {
{{0x33,0x8C}, {0x27,0x99}},
{{0x33,0x90}, {0x64,0x08}},
{{0x33,0x8C}, {0x27,0x9B}},
{{0x33,0x90}, {0x64,0x08}},
{{0x33,0x8C}, {0xA1,0x03}},
{{0x33,0x90}, {0x00,0x05}},
};

static struct regval_list sensor_colorfx_bw_regs[] = {
{{0x33,0x8C}, {0x27,0x99}},
{{0x33,0x90}, {0x64,0x09}},
{{0x33,0x8C}, {0x27,0x9B}},
{{0x33,0x90}, {0x64,0x09}},
{{0x33,0x8C}, {0xA1,0x03}},
{{0x33,0x90}, {0x00,0x05}},
};

static struct regval_list sensor_colorfx_sepia_regs[] = {
{{0x33,0x8C}, {0x27,0x99}},
{{0x33,0x90}, {0x64,0x0A}},
{{0x33,0x8C}, {0x27,0x9B}},
{{0x33,0x90}, {0x64,0x0A}},
{{0x33,0x8C}, {0xA1,0x03}},
{{0x33,0x90}, {0x00,0x05}},
};

static struct regval_list sensor_colorfx_negative_regs[] = {
{{0x33,0x8C}, {0x27,0x99}},
{{0x33,0x90}, {0x64,0x0B}},
{{0x33,0x8C}, {0x27,0x9B}},
{{0x33,0x90}, {0x64,0x0B}},
{{0x33,0x8C}, {0xA1,0x03}},
{{0x33,0x90}, {0x00,0x05}},
};

static struct regval_list sensor_colorfx_emboss_regs[] = {
	//NULL
};

static struct regval_list sensor_colorfx_sketch_regs[] = {
	//NULL
};

static struct regval_list sensor_colorfx_sky_blue_regs[] = {
	//NULL
};

static struct regval_list sensor_colorfx_grass_green_regs[] = {
	//NULL
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
	{{0x33,0x8c},{0x22,0x44}},
	{{0x33,0x90},{0x00,0x10}},
};

static struct regval_list sensor_ev_neg3_regs[] = {
	{{0x33,0x8c},{0x22,0x44}},
	{{0x33,0x90},{0x00,0x20}},
};

static struct regval_list sensor_ev_neg2_regs[] = {
	{{0x33,0x8c},{0x22,0x44}},
	{{0x33,0x90},{0x00,0x20}},
};

static struct regval_list sensor_ev_neg1_regs[] = {
	{{0x33,0x8c},{0x22,0x44}},
	{{0x33,0x90},{0x00,0x30}},
};

static struct regval_list sensor_ev_zero_regs[] = {
	{{0x33,0x8c},{0x22,0x44}},
	{{0x33,0x90},{0x00,0x3d}},
};

static struct regval_list sensor_ev_pos1_regs[] = {
	{{0x33,0x8c},{0x22,0x44}},
	{{0x33,0x90},{0x00,0x4d}},
};

static struct regval_list sensor_ev_pos2_regs[] = {
	{{0x33,0x8c},{0x22,0x44}},
	{{0x33,0x90},{0x00,0x5d}},
};

static struct regval_list sensor_ev_pos3_regs[] = {
	{{0x33,0x8c},{0x22,0x44}},
	{{0x33,0x90},{0x00,0x6d}},
};

static struct regval_list sensor_ev_pos4_regs[] = {
	{{0x33,0x8c},{0x22,0x44}},
	{{0x33,0x90},{0x00,0x7d}},
};


/*
 * Here we'll try to encapsulate the changes for just the output
 * video format.
 *
 */


static struct regval_list sensor_fmt_yuv422_yuyv[] = {
	{{0x33,0x8C}, {0x27,0x95}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x02}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x97}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x02}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x95}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x02}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x97}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x02}}, // SEQ_CMD
	{{0x33,0x8C}, {0xa1,0x03}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x05}}, // SEQ_CMD

};


static struct regval_list sensor_fmt_yuv422_yvyu[] = {
	{{0x33,0x8C}, {0x27,0x95}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x03}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x97}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x03}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x95}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x03}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x97}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x03}}, // SEQ_CMD
	{{0x33,0x8C}, {0xa1,0x03}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x05}}, // SEQ_CMD
};

static struct regval_list sensor_fmt_yuv422_vyuy[] = {
	{{0x33,0x8C}, {0x27,0x95}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x01}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x97}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x01}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x95}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x01}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x97}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x01}}, // SEQ_CMD
	{{0x33,0x8C}, {0xa1,0x03}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x05}}, // SEQ_CMD
};

static struct regval_list sensor_fmt_yuv422_uyvy[] = {
	{{0x33,0x8C}, {0x27,0x95}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x00}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x97}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x00}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x95}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x00}}, // SEQ_CMD
	{{0x33,0x8C}, {0x27,0x97}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x00}}, // SEQ_CMD
	{{0x33,0x8C}, {0xa1,0x03}}, // MCU_ADDRESS
	{{0x33,0x90}, {0x00,0x05}}, // SEQ_CMD
};

//static struct regval_list sensor_fmt_raw[] = {
//
//};



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

	data[REG_ADDR_STEP] = 0xff;
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
		if(vals->reg_num[0] == 0xff)
			msleep(vals->value[0] * 256 + vals->value[1]);
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
			msleep(200);
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

	regs.value[0] = 0x00;
	regs.value[1] = 0x00;

	regs.reg_num[0] = 0x30;
	regs.reg_num[1] = 0x00;
	ret = sensor_read(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_read err at sensor_detect!\n");
		return ret;
	}

	if(regs.value[0] != 0x15)//0x1580
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
//	{
//		.desc		= "Raw RGB Bayer",
//		.mbus_code	= V4L2_MBUS_FMT_SBGGR8_1X8,//linux-3.0
//		.regs 		= sensor_fmt_raw,
//		.regs_size = ARRAY_SIZE(sensor_fmt_raw),
//		.bpp		= 1
//	},
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
		.width		= UXGA_WIDTH,
		.height		= UXGA_HEIGHT,
		.regs 		= sensor_uxga_regs,
		.regs_size	= ARRAY_SIZE(sensor_uxga_regs),
		.set_size		= NULL,
	},
	/* VGA */
	{
		.width		= VGA_WIDTH,
		.height		= VGA_HEIGHT,
		.regs 		= sensor_vga_regs,
		.regs_size	= ARRAY_SIZE(sensor_vga_regs),
		.set_size		= NULL,
	}
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
	struct v4l2_captureparm *cp = &parms->parm.capture;
	//struct sensor_info *info = to_state(sd);

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe.numerator = 1;
	cp->timeperframe.denominator = SENSOR_FRAME_RATE;

	return 0;
}

static int sensor_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
//	struct v4l2_captureparm *cp = &parms->parm.capture;
	//struct v4l2_fract *tpf = &cp->timeperframe;
	//struct sensor_info *info = to_state(sd);
	//int div;

//	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
//		return -EINVAL;
//	if (cp->extendedmode != 0)
//		return -EINVAL;

//	if (tpf->numerator == 0 || tpf->denominator == 0)
//		div = 1;  /* Reset to full rate */
//	else
//		div = (tpf->numerator*SENSOR_FRAME_RATE)/tpf->denominator;
//
//	if (div == 0)
//		div = 1;
//	else if (div > CLK_SCALE)
//		div = CLK_SCALE;
//	info->clkrc = (info->clkrc & 0x80) | div;
//	tpf->numerator = 1;
//	tpf->denominator = sensor_FRAME_RATE/div;
//sensor_write(sd, REG_CLKRC, info->clkrc);
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
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 0);
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
	int ret;
	struct sensor_info *info = to_state(sd);
	struct regval_list regs;

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x8c;
	regs.value[0] = 0x27;
	regs.value[1] = 0x19;

	ret = sensor_write(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_write err at sensor_s_hflip!\n");
		return ret;
	}

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x90;

	ret = sensor_read(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_read err at sensor_g_hflip!\n");
		return ret;
	}

	regs.value[1] &= (1<<0); //bit0 is hflip enable

	*value = regs.value[1];
	info->hflip = *value;
	return 0;
}

static int sensor_s_hflip(struct v4l2_subdev *sd, int value)
{
	int ret;
	struct sensor_info *info = to_state(sd);
	struct regval_list regs;

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x8C;
	regs.value[0] = 0x27;
	regs.value[1] = 0x19;

	ret = sensor_write(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_write err at sensor_s_hflip!\n");
		return ret;
	}

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x90;

	ret = sensor_read(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_read err at sensor_s_hflip!\n");
		return ret;
	}

	switch(value) {
	case 0:
		regs.value[1] &= 0xfe;
		break;
	case 1:
		regs.value[1] |= 0x01;
		break;
	default:
		break;
	}

	ret = sensor_write(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_write err at sensor_s_hflip!\n");
		return ret;
	}

	msleep(100);

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x8C;
	regs.value[0] = 0x27;
	regs.value[1] = 0x3B;

	ret = sensor_write(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_write err at sensor_s_hflip!\n");
		return ret;
	}

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x90;

	ret = sensor_read(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_read err at sensor_s_hflip!\n");
		return ret;
	}

	switch(value) {
	case 0:
		regs.value[1] &= 0xfe;
		break;
	case 1:
		regs.value[1] |= 0x01;
		break;
	default:
		break;
	}

	ret = sensor_write(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_write err at sensor_s_hflip!\n");
		return ret;
	}

	msleep(100);
	info->hflip = value;
	return 0;
}

static int sensor_g_vflip(struct v4l2_subdev *sd, __s32 *value)
{
	int ret;
	struct sensor_info *info = to_state(sd);
	struct regval_list regs;

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x8c;
	regs.value[0] = 0x27;
	regs.value[1] = 0x19;

	ret = sensor_write(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_write err at sensor_s_vflip!\n");
		return ret;
	}

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x90;

	ret = sensor_read(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_read err at sensor_g_vflip!\n");
		return ret;
	}

	regs.value[1] &= (1<<1); //bit1 is vflip enable

	*value = regs.value[1];
	info->vflip = *value;
	return 0;
}

static int sensor_s_vflip(struct v4l2_subdev *sd, int value)
{
	int ret;
	struct sensor_info *info = to_state(sd);
	struct regval_list regs;

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x8C;
	regs.value[0] = 0x27;
	regs.value[1] = 0x19;

	ret = sensor_write(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_write err at sensor_s_vflip!\n");
		return ret;
	}

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x90;

	ret = sensor_read(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_read err at sensor_s_vflip!\n");
		return ret;
	}

	switch(value) {
	case 0:
		regs.value[1] &= 0xfd;
		break;
	case 1:
		regs.value[1] |= 0x02;
		break;
	default:
		break;
	}

	ret = sensor_write(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_write err at sensor_s_vflip!\n");
		return ret;
	}

	msleep(100);

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x8C;
	regs.value[0] = 0x27;
	regs.value[1] = 0x3B;

	ret = sensor_write(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_write err at sensor_s_vflip!\n");
		return ret;
	}

	regs.reg_num[0] = 0x33;
	regs.reg_num[1] = 0x90;

	ret = sensor_read(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_read err at sensor_s_vflip!\n");
		return ret;
	}

	switch(value) {
	case 0:
		regs.value[1] &= 0xfd;
		break;
	case 1:
		regs.value[1] |= 0x02;
		break;
	default:
		break;
	}

	ret = sensor_write(sd, regs.reg_num, regs.value);
	if (ret < 0) {
		csi_dev_err("sensor_write err at sensor_s_vflip!\n");
		return ret;
	}

	msleep(100);
	info->vflip = value;
	return 0;
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
	struct sensor_info *info = to_state(sd);

	*value = info->autowb ;
	return 0;
}

static int sensor_s_autowb(struct v4l2_subdev *sd, int value)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	ret = sensor_write_array(sd, sensor_wb_auto_regs, ARRAY_SIZE(sensor_wb_auto_regs));
	if (ret < 0) {
		csi_dev_err("sensor_write_array err at sensor_s_autowb!\n");
		return ret;
	}

	msleep(60);

	info->autowb = value;
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
	{ "mt9d112", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

//linux-3.0
static struct i2c_driver sensor_driver = {
	.driver = {
		.owner = THIS_MODULE,
	.name = "mt9d112",
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


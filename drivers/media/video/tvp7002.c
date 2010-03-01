/* Texas Instruments Triple 8-/10-BIT 165-/110-MSPS Video and Graphics
 * Digitizer with Horizontal PLL registers
 *
 * Copyright (C) 2009 Texas Instruments Inc
 * Author: Santiago Nunez-Corrales <santiago.nunez@ridgerun.com>
 *
 * This code is partially based upon the TVP5150 driver
 * written by Mauro Carvalho Chehab (mchehab@infradead.org),
 * the TVP514x driver written by Vaibhav Hiremath <hvaibhav@ti.com>
 * and the TVP7002 driver in the TI LSP 2.10.00.14. Revisions by
 * Muralidharan Karicheri and Snehaprabha Narnakaje (TI).
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/tvp7002.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-common.h>
#include "tvp7002_reg.h"

MODULE_DESCRIPTION("TI TVP7002 Video and Graphics Digitizer driver");
MODULE_AUTHOR("Santiago Nunez-Corrales <santiago.nunez@ridgerun.com>");
MODULE_LICENSE("GPL");

/* Module Name */
#define TVP7002_MODULE_NAME	"tvp7002"

/* I2C retry attempts */
#define I2C_RETRY_COUNT		(5)

/* End of registers */
#define TVP7002_EOR		0x5c

/* Read write definition for registers */
#define TVP7002_READ		0
#define TVP7002_WRITE		1
#define TVP7002_RESERVED	2

/* Interlaced vs progressive mask and shift */
#define TVP7002_IP_SHIFT	5
#define TVP7002_INPR_MASK	(0x01 << TVP7002_IP_SHIFT)

/* Shift for CPL and LPF registers */
#define TVP7002_CL_SHIFT	8
#define TVP7002_CL_MASK		0x0f

/* Debug functions */
static int debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-2)");

/* Structure for register values */
struct i2c_reg_value {
	u8 reg;
	u8 value;
	u8 type;
};

/*
 * Register default values (according to tvp7002 datasheet)
 * In the case of read-only registers, the value (0xff) is
 * never written. R/W functionality is controlled by the
 * writable bit in the register struct definition.
 */
static const struct i2c_reg_value tvp7002_init_default[] = {
	{ TVP7002_CHIP_REV, 0xff, TVP7002_READ },
	{ TVP7002_HPLL_FDBK_DIV_MSBS, 0x67, TVP7002_WRITE },
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x20, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0xa0, TVP7002_WRITE },
	{ TVP7002_HPLL_PHASE_SEL, 0x80, TVP7002_WRITE },
	{ TVP7002_CLAMP_START, 0x32, TVP7002_WRITE },
	{ TVP7002_CLAMP_W, 0x20, TVP7002_WRITE },
	{ TVP7002_HSYNC_OUT_W, 0x60, TVP7002_WRITE },
	{ TVP7002_B_FINE_GAIN, 0x00, TVP7002_WRITE },
	{ TVP7002_G_FINE_GAIN, 0x00, TVP7002_WRITE },
	{ TVP7002_R_FINE_GAIN, 0x00, TVP7002_WRITE },
	{ TVP7002_B_FINE_OFF_MSBS, 0x80, TVP7002_WRITE },
	{ TVP7002_G_FINE_OFF_MSBS, 0x80, TVP7002_WRITE },
	{ TVP7002_R_FINE_OFF_MSBS, 0x80, TVP7002_WRITE },
	{ TVP7002_SYNC_CTL_1, 0x20, TVP7002_WRITE },
	{ TVP7002_HPLL_AND_CLAMP_CTL, 0x2e, TVP7002_WRITE },
	{ TVP7002_SYNC_ON_G_THRS, 0x5d, TVP7002_WRITE },
	{ TVP7002_SYNC_SEPARATOR_THRS, 0x47, TVP7002_WRITE },
	{ TVP7002_HPLL_PRE_COAST, 0x00, TVP7002_WRITE },
	{ TVP7002_HPLL_POST_COAST, 0x00, TVP7002_WRITE },
	{ TVP7002_SYNC_DETECT_STAT, 0xff, TVP7002_READ },
	{ TVP7002_OUT_FORMATTER, 0x47, TVP7002_WRITE },
	{ TVP7002_MISC_CTL_1, 0x01, TVP7002_WRITE },
	{ TVP7002_MISC_CTL_2, 0x00, TVP7002_WRITE },
	{ TVP7002_MISC_CTL_3, 0x01, TVP7002_WRITE },
	{ TVP7002_IN_MUX_SEL_1, 0x00, TVP7002_WRITE },
	{ TVP7002_IN_MUX_SEL_2, 0x67, TVP7002_WRITE },
	{ TVP7002_B_AND_G_COARSE_GAIN, 0x77, TVP7002_WRITE },
	{ TVP7002_R_COARSE_GAIN, 0x07, TVP7002_WRITE },
	{ TVP7002_FINE_OFF_LSBS, 0x00, TVP7002_WRITE },
	{ TVP7002_B_COARSE_OFF, 0x10, TVP7002_WRITE },
	{ TVP7002_G_COARSE_OFF, 0x10, TVP7002_WRITE },
	{ TVP7002_R_COARSE_OFF, 0x10, TVP7002_WRITE },
	{ TVP7002_HSOUT_OUT_START, 0x08, TVP7002_WRITE },
	{ TVP7002_MISC_CTL_4, 0x00, TVP7002_WRITE },
	{ TVP7002_B_DGTL_ALC_OUT_LSBS, 0xff, TVP7002_READ },
	{ TVP7002_G_DGTL_ALC_OUT_LSBS, 0xff, TVP7002_READ },
	{ TVP7002_R_DGTL_ALC_OUT_LSBS, 0xff, TVP7002_READ },
	{ TVP7002_AUTO_LVL_CTL_ENABLE, 0x80, TVP7002_WRITE },
	{ TVP7002_DGTL_ALC_OUT_MSBS, 0xff, TVP7002_READ },
	{ TVP7002_AUTO_LVL_CTL_FILTER, 0x53, TVP7002_WRITE },
	{ 0x29, 0x08, TVP7002_RESERVED },
	{ TVP7002_FINE_CLAMP_CTL, 0x07, TVP7002_WRITE },
	/* PWR_CTL is controlled only by the probe and reset functions */
	{ TVP7002_PWR_CTL, 0x00, TVP7002_RESERVED },
	{ TVP7002_ADC_SETUP, 0x50, TVP7002_WRITE },
	{ TVP7002_COARSE_CLAMP_CTL, 0x00, TVP7002_WRITE },
	{ TVP7002_SOG_CLAMP, 0x80, TVP7002_WRITE },
	{ TVP7002_RGB_COARSE_CLAMP_CTL, 0x00, TVP7002_WRITE },
	{ TVP7002_SOG_COARSE_CLAMP_CTL, 0x04, TVP7002_WRITE },
	{ TVP7002_ALC_PLACEMENT, 0x5a, TVP7002_WRITE },
	{ 0x32, 0x18, TVP7002_RESERVED },
	{ 0x33, 0x60, TVP7002_RESERVED },
	{ TVP7002_MVIS_STRIPPER_W, 0xff, TVP7002_RESERVED },
	{ TVP7002_VSYNC_ALGN, 0x10, TVP7002_WRITE },
	{ TVP7002_SYNC_BYPASS, 0x00, TVP7002_WRITE },
	{ TVP7002_L_FRAME_STAT_LSBS, 0xff, TVP7002_READ },
	{ TVP7002_L_FRAME_STAT_MSBS, 0xff, TVP7002_READ },
	{ TVP7002_CLK_L_STAT_LSBS, 0xff, TVP7002_READ },
	{ TVP7002_CLK_L_STAT_MSBS, 0xff, TVP7002_READ },
	{ TVP7002_HSYNC_W, 0xff, TVP7002_READ },
	{ TVP7002_VSYNC_W, 0xff, TVP7002_READ },
	{ TVP7002_L_LENGTH_TOL, 0x03, TVP7002_WRITE },
	{ 0x3e, 0x60, TVP7002_RESERVED },
	{ TVP7002_VIDEO_BWTH_CTL, 0x01, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_LSBS, 0x01, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_MSBS, 0x2c, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_LSBS, 0x06, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_MSBS, 0x2c, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_START_L_OFF, 0x05, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_START_L_OFF, 0x00, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_DURATION, 0x1e, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_DURATION, 0x00, TVP7002_WRITE },
	{ TVP7002_FBIT_F_0_START_L_OFF, 0x00, TVP7002_WRITE },
	{ TVP7002_FBIT_F_1_START_L_OFF, 0x00, TVP7002_WRITE },
	{ TVP7002_YUV_Y_G_COEF_LSBS, 0xe3, TVP7002_WRITE },
	{ TVP7002_YUV_Y_G_COEF_MSBS, 0x16, TVP7002_WRITE },
	{ TVP7002_YUV_Y_B_COEF_LSBS, 0x4f, TVP7002_WRITE },
	{ TVP7002_YUV_Y_B_COEF_MSBS, 0x02, TVP7002_WRITE },
	{ TVP7002_YUV_Y_R_COEF_LSBS, 0xce, TVP7002_WRITE },
	{ TVP7002_YUV_Y_R_COEF_MSBS, 0x06, TVP7002_WRITE },
	{ TVP7002_YUV_U_G_COEF_LSBS, 0xab, TVP7002_WRITE },
	{ TVP7002_YUV_U_G_COEF_MSBS, 0xf3, TVP7002_WRITE },
	{ TVP7002_YUV_U_B_COEF_LSBS, 0x00, TVP7002_WRITE },
	{ TVP7002_YUV_U_B_COEF_MSBS, 0x10, TVP7002_WRITE },
	{ TVP7002_YUV_U_R_COEF_LSBS, 0x55, TVP7002_WRITE },
	{ TVP7002_YUV_U_R_COEF_MSBS, 0xfc, TVP7002_WRITE },
	{ TVP7002_YUV_V_G_COEF_LSBS, 0x78, TVP7002_WRITE },
	{ TVP7002_YUV_V_G_COEF_MSBS, 0xf1, TVP7002_WRITE },
	{ TVP7002_YUV_V_B_COEF_LSBS, 0x88, TVP7002_WRITE },
	{ TVP7002_YUV_V_B_COEF_MSBS, 0xfe, TVP7002_WRITE },
	{ TVP7002_YUV_V_R_COEF_LSBS, 0x00, TVP7002_WRITE },
	{ TVP7002_YUV_V_R_COEF_MSBS, 0x10, TVP7002_WRITE },
	/* This signals end of register values */
	{ TVP7002_EOR, 0xff, TVP7002_RESERVED }
};

/* Register parameters for 480P */
static const struct i2c_reg_value tvp7002_parms_480P[] = {
	{ TVP7002_HPLL_FDBK_DIV_MSBS, 0x35, TVP7002_WRITE },
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x0a, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0x02, TVP7002_WRITE },
	{ TVP7002_HPLL_PHASE_SEL, 0x14, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_LSBS, 0x91, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_MSBS, 0x00, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_LSBS, 0x0B, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_MSBS, 0x00, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_START_L_OFF, 0x03, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_START_L_OFF, 0x01, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_DURATION, 0x13, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_DURATION, 0x13, TVP7002_WRITE },
	{ TVP7002_ALC_PLACEMENT, 0x18, TVP7002_WRITE },
	{ TVP7002_CLAMP_START, 0x06, TVP7002_WRITE },
	{ TVP7002_CLAMP_W, 0x10, TVP7002_WRITE },
	{ TVP7002_HPLL_PRE_COAST, 0x03, TVP7002_WRITE },
	{ TVP7002_HPLL_POST_COAST, 0x03, TVP7002_WRITE },
	{ TVP7002_EOR, 0xff, TVP7002_RESERVED }
};

/* Register parameters for 576P */
static const struct i2c_reg_value tvp7002_parms_576P[] = {
	{ TVP7002_HPLL_FDBK_DIV_MSBS, 0x36, TVP7002_WRITE },
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x00, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0x18, TVP7002_WRITE },
	{ TVP7002_HPLL_PHASE_SEL, 0x14, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_LSBS, 0x9B, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_MSBS, 0x00, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_LSBS, 0x0F, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_MSBS, 0x00, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_START_L_OFF, 0x00, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_START_L_OFF, 0x00, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_DURATION, 0x2D, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_DURATION, 0x00, TVP7002_WRITE },
	{ TVP7002_ALC_PLACEMENT, 0x18, TVP7002_WRITE },
	{ TVP7002_CLAMP_START, 0x06, TVP7002_WRITE },
	{ TVP7002_CLAMP_W, 0x10, TVP7002_WRITE },
	{ TVP7002_HPLL_PRE_COAST, 0x03, TVP7002_WRITE },
	{ TVP7002_HPLL_POST_COAST, 0x03, TVP7002_WRITE },
	{ TVP7002_EOR, 0xff, TVP7002_RESERVED }
};

/* Register parameters for 1080I60 */
static const struct i2c_reg_value tvp7002_parms_1080I60[] = {
	{ TVP7002_HPLL_FDBK_DIV_MSBS, 0x89, TVP7002_WRITE },
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x08, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0x98, TVP7002_WRITE },
	{ TVP7002_HPLL_PHASE_SEL, 0x14, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_LSBS, 0x06, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_MSBS, 0x01, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_LSBS, 0x8a, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_MSBS, 0x08, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_START_L_OFF, 0x02, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_START_L_OFF, 0x02, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_DURATION, 0x16, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_DURATION, 0x17, TVP7002_WRITE },
	{ TVP7002_ALC_PLACEMENT, 0x5a, TVP7002_WRITE },
	{ TVP7002_CLAMP_START, 0x32, TVP7002_WRITE },
	{ TVP7002_CLAMP_W, 0x20, TVP7002_WRITE },
	{ TVP7002_HPLL_PRE_COAST, 0x01, TVP7002_WRITE },
	{ TVP7002_HPLL_POST_COAST, 0x00, TVP7002_WRITE },
	{ TVP7002_EOR, 0xff, TVP7002_RESERVED }
};

/* Register parameters for 1080P60 */
static const struct i2c_reg_value tvp7002_parms_1080P60[] = {
	{ TVP7002_HPLL_FDBK_DIV_MSBS, 0x89, TVP7002_WRITE },
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x08, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0xE0, TVP7002_WRITE },
	{ TVP7002_HPLL_PHASE_SEL, 0x14, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_LSBS, 0x06, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_MSBS, 0x01, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_LSBS, 0x8a, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_MSBS, 0x08, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_START_L_OFF, 0x02, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_START_L_OFF, 0x02, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_DURATION, 0x16, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_DURATION, 0x17, TVP7002_WRITE },
	{ TVP7002_ALC_PLACEMENT, 0x5a, TVP7002_WRITE },
	{ TVP7002_CLAMP_START, 0x32, TVP7002_WRITE },
	{ TVP7002_CLAMP_W, 0x20, TVP7002_WRITE },
	{ TVP7002_HPLL_PRE_COAST, 0x01, TVP7002_WRITE },
	{ TVP7002_HPLL_POST_COAST, 0x00, TVP7002_WRITE },
	{ TVP7002_EOR, 0xff, TVP7002_RESERVED }
};

/* Register parameters for 1080I50 */
static const struct i2c_reg_value tvp7002_parms_1080I50[] = {
	{ TVP7002_HPLL_FDBK_DIV_MSBS, 0xa5, TVP7002_WRITE },
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x00, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0x98, TVP7002_WRITE },
	{ TVP7002_HPLL_PHASE_SEL, 0x14, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_LSBS, 0x06, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_MSBS, 0x01, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_LSBS, 0x8a, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_MSBS, 0x08, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_START_L_OFF, 0x02, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_START_L_OFF, 0x02, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_DURATION, 0x16, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_DURATION, 0x17, TVP7002_WRITE },
	{ TVP7002_ALC_PLACEMENT, 0x5a, TVP7002_WRITE },
	{ TVP7002_CLAMP_START, 0x32, TVP7002_WRITE },
	{ TVP7002_CLAMP_W, 0x20, TVP7002_WRITE },
	{ TVP7002_HPLL_PRE_COAST, 0x01, TVP7002_WRITE },
	{ TVP7002_HPLL_POST_COAST, 0x00, TVP7002_WRITE },
	{ TVP7002_EOR, 0xff, TVP7002_RESERVED }
};

/* Register parameters for 720P60 */
static const struct i2c_reg_value tvp7002_parms_720P60[] = {
	{ TVP7002_HPLL_FDBK_DIV_MSBS, 0x67, TVP7002_WRITE },
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x02, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0xa0, TVP7002_WRITE },
	{ TVP7002_HPLL_PHASE_SEL, 0x16, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_LSBS, 0x47, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_MSBS, 0x01, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_LSBS, 0x4B, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_MSBS, 0x06, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_START_L_OFF, 0x05, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_START_L_OFF, 0x00, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_DURATION, 0x2D, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_DURATION, 0x00, TVP7002_WRITE },
	{ TVP7002_ALC_PLACEMENT, 0x5a, TVP7002_WRITE },
	{ TVP7002_CLAMP_START, 0x32, TVP7002_WRITE },
	{ TVP7002_CLAMP_W, 0x20, TVP7002_WRITE },
	{ TVP7002_HPLL_PRE_COAST, 0x00, TVP7002_WRITE },
	{ TVP7002_HPLL_POST_COAST, 0x00, TVP7002_WRITE },
	{ TVP7002_EOR, 0xff, TVP7002_RESERVED }
};

/* Register parameters for 720P50 */
static const struct i2c_reg_value tvp7002_parms_720P50[] = {
	{ TVP7002_HPLL_FDBK_DIV_MSBS, 0x7b, TVP7002_WRITE },
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x0c, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0x98, TVP7002_WRITE },
	{ TVP7002_HPLL_PHASE_SEL, 0x16, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_LSBS, 0x47, TVP7002_WRITE },
	{ TVP7002_AVID_START_PIXEL_MSBS, 0x01, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_LSBS, 0x4B, TVP7002_WRITE },
	{ TVP7002_AVID_STOP_PIXEL_MSBS, 0x06, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_START_L_OFF, 0x05, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_START_L_OFF, 0x00, TVP7002_WRITE },
	{ TVP7002_VBLK_F_0_DURATION, 0x2D, TVP7002_WRITE },
	{ TVP7002_VBLK_F_1_DURATION, 0x00, TVP7002_WRITE },
	{ TVP7002_ALC_PLACEMENT, 0x5a, TVP7002_WRITE },
	{ TVP7002_CLAMP_START, 0x32, TVP7002_WRITE },
	{ TVP7002_CLAMP_W, 0x20, TVP7002_WRITE },
	{ TVP7002_HPLL_PRE_COAST, 0x01, TVP7002_WRITE },
	{ TVP7002_HPLL_POST_COAST, 0x00, TVP7002_WRITE },
	{ TVP7002_EOR, 0xff, TVP7002_RESERVED }
};

/* Struct list for available formats */
static const struct v4l2_fmtdesc tvp7002_fmt_list[] = {
	{
	 .index = 0,
	 .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	 .flags = 0,
	 .description = "8-bit UYVY 4:2:2 Format",
	 .pixelformat = V4L2_PIX_FMT_UYVY,
	},
};

#define NUM_FORMATS		ARRAY_SIZE(tvp7002_fmt_list)

/* Preset definition for handling device operation */
struct tvp7002_preset_definition {
	u32 preset;
	const struct i2c_reg_value *p_settings;
	enum v4l2_colorspace color_space;
	enum v4l2_field scanmode;
	u16 progressive;
	u16 lines_per_frame;
	u16 cpl_min;
	u16 cpl_max;
};

/* Struct list for digital video presets */
static const struct tvp7002_preset_definition tvp7002_presets[] = {
	{
		V4L2_DV_720P60,
		tvp7002_parms_720P60,
		V4L2_COLORSPACE_REC709,
		V4L2_FIELD_NONE,
		1,
		0x2EE,
		135,
		153
	},
	{
		V4L2_DV_1080I60,
		tvp7002_parms_1080I60,
		V4L2_COLORSPACE_REC709,
		V4L2_FIELD_INTERLACED,
		0,
		0x465,
		181,
		205
	},
	{
		V4L2_DV_1080I50,
		tvp7002_parms_1080I50,
		V4L2_COLORSPACE_REC709,
		V4L2_FIELD_INTERLACED,
		0,
		0x465,
		217,
		245
	},
	{
		V4L2_DV_720P50,
		tvp7002_parms_720P50,
		V4L2_COLORSPACE_REC709,
		V4L2_FIELD_NONE,
		1,
		0x2EE,
		163,
		183
	},
	{
		V4L2_DV_1080P60,
		tvp7002_parms_1080P60,
		V4L2_COLORSPACE_REC709,
		V4L2_FIELD_NONE,
		1,
		0x465,
		90,
		102
	},
	{
		V4L2_DV_480P59_94,
		tvp7002_parms_480P,
		V4L2_COLORSPACE_SMPTE170M,
		V4L2_FIELD_NONE,
		1,
		0x20D,
		0xffff,
		0xffff
	},
	{
		V4L2_DV_576P50,
		tvp7002_parms_576P,
		V4L2_COLORSPACE_SMPTE170M,
		V4L2_FIELD_NONE,
		1,
		0x271,
		0xffff,
		0xffff
	}
};

#define NUM_PRESETS	ARRAY_SIZE(tvp7002_presets)

/* Device definition */
struct tvp7002 {
	struct v4l2_subdev sd;
	const struct tvp7002_config *pdata;

	int ver;
	int streaming;

	struct v4l2_pix_format pix;
	const struct tvp7002_preset_definition *current_preset;
	u8 gain;
};

/*
 * to_tvp7002 - Obtain device handler TVP7002
 * @sd: ptr to v4l2_subdev struct
 *
 * Returns device handler tvp7002.
 */
static inline struct tvp7002 *to_tvp7002(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tvp7002, sd);
}

/*
 * tvp7002_read - Read a value from a register in an TVP7002
 * @sd: ptr to v4l2_subdev struct
 * @reg: TVP7002 register address
 * @dst: pointer to 8-bit destination
 *
 * Returns value read if successful, or non-zero (-1) otherwise.
 */
static int tvp7002_read(struct v4l2_subdev *sd, u8 addr, u8 *dst)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	int retry;
	int error;

	for (retry = 0; retry < I2C_RETRY_COUNT; retry++) {
		error = i2c_smbus_read_byte_data(c, addr);

		if (error >= 0) {
			*dst = (u8)error;
			return 0;
		}

		msleep_interruptible(10);
	}
	v4l2_err(sd, "TVP7002 read error %d\n", error);
	return error;
}

/*
 * tvp7002_read_err() - Read a register value with error code
 * @sd: pointer to standard V4L2 sub-device structure
 * @reg: destination register
 * @val: value to be read
 * @error: pointer to error value
 *
 * Read a value in a register and save error value in pointer.
 * Also update the register table if successful
 */
static inline void tvp7002_read_err(struct v4l2_subdev *sd, u8 reg,
							u8 *dst, int *err)
{
	if (!*err)
		*err = tvp7002_read(sd, reg, dst);
}

/*
 * tvp7002_write() - Write a value to a register in TVP7002
 * @sd: ptr to v4l2_subdev struct
 * @addr: TVP7002 register address
 * @value: value to be written to the register
 *
 * Write a value to a register in an TVP7002 decoder device.
 * Returns zero if successful, or non-zero otherwise.
 */
static int tvp7002_write(struct v4l2_subdev *sd, u8 addr, u8 value)
{
	struct i2c_client *c;
	int retry;
	int error;

	c = v4l2_get_subdevdata(sd);

	for (retry = 0; retry < I2C_RETRY_COUNT; retry++) {
		error = i2c_smbus_write_byte_data(c, addr, value);

		if (error >= 0)
			return 0;

		v4l2_warn(sd, "Write: retry ... %d\n", retry);
		msleep_interruptible(10);
	}
	v4l2_err(sd, "TVP7002 write error %d\n", error);
	return error;
}

/*
 * tvp7002_write_err() - Write a register value with error code
 * @sd: pointer to standard V4L2 sub-device structure
 * @reg: destination register
 * @val: value to be written
 * @error: pointer to error value
 *
 * Write a value in a register and save error value in pointer.
 * Also update the register table if successful
 */
static inline void tvp7002_write_err(struct v4l2_subdev *sd, u8 reg,
							u8 val, int *err)
{
	if (!*err)
		*err = tvp7002_write(sd, reg, val);
}

/*
 * tvp7002_g_chip_ident() - Get chip identification number
 * @sd: ptr to v4l2_subdev struct
 * @chip: ptr to v4l2_dbg_chip_ident struct
 *
 * Obtains the chip's identification number.
 * Returns zero or -EINVAL if read operation fails.
 */
static int tvp7002_g_chip_ident(struct v4l2_subdev *sd,
					struct v4l2_dbg_chip_ident *chip)
{
	u8 rev;
	int error;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	error = tvp7002_read(sd, TVP7002_CHIP_REV, &rev);

	if (error < 0)
		return error;

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_TVP7002, rev);
}

/*
 * tvp7002_write_inittab() - Write initialization values
 * @sd: ptr to v4l2_subdev struct
 * @regs: ptr to i2c_reg_value struct
 *
 * Write initialization values.
 * Returns zero or -EINVAL if read operation fails.
 */
static int tvp7002_write_inittab(struct v4l2_subdev *sd,
					const struct i2c_reg_value *regs)
{
	int error = 0;

	/* Initialize the first (defined) registers */
	while (TVP7002_EOR != regs->reg) {
		if (TVP7002_WRITE == regs->type)
			tvp7002_write_err(sd, regs->reg, regs->value, &error);
		regs++;
	}

	return error;
}

/*
 * tvp7002_s_dv_preset() - Set digital video preset
 * @sd: ptr to v4l2_subdev struct
 * @std: ptr to v4l2_dv_preset struct
 *
 * Set the digital video preset for a TVP7002 decoder device.
 * Returns zero when successful or -EINVAL if register access fails.
 */
static int tvp7002_s_dv_preset(struct v4l2_subdev *sd,
					struct v4l2_dv_preset *dv_preset)
{
	struct tvp7002 *device = to_tvp7002(sd);
	u32 preset;
	int i;

	for (i = 0; i < NUM_PRESETS; i++) {
		preset = tvp7002_presets[i].preset;
		if (preset == dv_preset->preset) {
			device->current_preset = &tvp7002_presets[i];
			return tvp7002_write_inittab(sd, tvp7002_presets[i].p_settings);
		}
	}

	return -EINVAL;
}

/*
 * tvp7002_g_ctrl() - Get a control
 * @sd: ptr to v4l2_subdev struct
 * @ctrl: ptr to v4l2_control struct
 *
 * Get a control for a TVP7002 decoder device.
 * Returns zero when successful or -EINVAL if register access fails.
 */
static int tvp7002_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct tvp7002 *device = to_tvp7002(sd);

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		ctrl->value = device->gain;
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * tvp7002_s_ctrl() - Set a control
 * @sd: ptr to v4l2_subdev struct
 * @ctrl: ptr to v4l2_control struct
 *
 * Set a control in TVP7002 decoder device.
 * Returns zero when successful or -EINVAL if register access fails.
 */
static int tvp7002_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct tvp7002 *device = to_tvp7002(sd);
	int error = 0;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		tvp7002_write_err(sd, TVP7002_R_FINE_GAIN,
						ctrl->value & 0xff, &error);
		tvp7002_write_err(sd, TVP7002_G_FINE_GAIN,
						ctrl->value & 0xff, &error);
		tvp7002_write_err(sd, TVP7002_B_FINE_GAIN,
						ctrl->value & 0xff, &error);

		if (error < 0)
			return error;

		/* Set only after knowing there is no error */
		device->gain = ctrl->value & 0xff;
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * tvp7002_queryctrl() - Query a control
 * @sd: ptr to v4l2_subdev struct
 * @ctrl: ptr to v4l2_queryctrl struct
 *
 * Query a control of a TVP7002 decoder device.
 * Returns zero when successful or -EINVAL if register read fails.
 */
static int tvp7002_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_GAIN:
		/*
		 * Gain is supported [0-255, default=0, step=1]
		 */
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 0);
	default:
		return -EINVAL;
	}
}

/*
 * tvp7002_try_fmt_cap() - V4L2 decoder interface handler for try_fmt
 * @sd: pointer to standard V4L2 sub-device structure
 * @f: pointer to standard V4L2 VIDIOC_TRY_FMT ioctl structure
 *
 * Implement the VIDIOC_TRY_FMT ioctl for the CAPTURE buffer type. This
 * ioctl is used to negotiate the image capture size and pixel format
 * without actually making it take effect.
 */
static int tvp7002_try_fmt_cap(struct v4l2_subdev *sd, struct v4l2_format *f)
{
	struct tvp7002 *device = to_tvp7002(sd);
	struct v4l2_dv_enum_preset e_preset;
	struct v4l2_pix_format *pix;
	int error = 0;

	pix = &f->fmt.pix;

	/* Calculate height and width based on current standard */
	error = v4l_fill_dv_preset_info(device->current_preset->preset, &e_preset);
	if (error)
		return -EINVAL;

	pix->width = e_preset.width;
	pix->height = e_preset.height;
	pix->pixelformat = V4L2_PIX_FMT_UYVY;
	pix->field = device->current_preset->scanmode;
	pix->bytesperline = pix->width * 2;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->colorspace = device->current_preset->color_space;
	pix->priv = 0;

	v4l2_dbg(1, debug, sd, "Try FMT: pixelformat - %s, bytesperline - %d"
			"Width - %d, Height - %d", "8-bit UYVY 4:2:2 Format",
			pix->bytesperline, pix->width, pix->height);
	return error;
}

/*
 * tvp7002_s_fmt() - V4L2 decoder interface handler for s_fmt
 * @sd: pointer to standard V4L2 sub-device structure
 * @f: pointer to standard V4L2 VIDIOC_S_FMT ioctl structure
 *
 * If the requested format is supported, configures the HW to use that
 * format, returns error code if format not supported or HW can't be
 * correctly configured.
 */
static int tvp7002_s_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
	struct tvp7002 *decoder = to_tvp7002(sd);
	int rval;

	rval = tvp7002_try_fmt_cap(sd, f);
	if (!rval)
		decoder->pix = f->fmt.pix;
	return rval;
}

/*
 * tvp7002_g_fmt() - V4L2 decoder interface handler for tvp7002_g_fmt
 * @sd: pointer to standard V4L2 sub-device structure
 * @f: pointer to standard V4L2 v4l2_format structure
 *
 * Returns the decoder's current pixel format in the v4l2_format
 * parameter.
 */
static int tvp7002_g_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
	struct tvp7002 *decoder = to_tvp7002(sd);

	f->fmt.pix = decoder->pix;

	v4l2_dbg(1, debug, sd, "Current FMT: bytesperline - %d"
			"Width - %d, Height - %d",
			decoder->pix.bytesperline,
			decoder->pix.width, decoder->pix.height);
	return 0;
}

/*
 * tvp7002_query_dv_preset() - query DV preset
 * @sd: pointer to standard V4L2 sub-device structure
 * @std_id: standard V4L2 v4l2_dv_preset
 *
 * Returns the current DV preset by TVP7002. If no active input is
 * detected, returns -EINVAL
 */
static int tvp7002_query_dv_preset(struct v4l2_subdev *sd,
						struct v4l2_dv_preset *qpreset)
{
	const struct tvp7002_preset_definition *presets = tvp7002_presets;
	struct v4l2_dv_enum_preset e_preset;
	struct tvp7002 *device;
	u8 progressive;
	u32 lpfr;
	u32 cpln;
	int error = 0;
	u8 lpf_lsb;
	u8 lpf_msb;
	u8 cpl_lsb;
	u8 cpl_msb;
	int index;

	device = to_tvp7002(sd);

	/* Read standards from device registers */
	tvp7002_read_err(sd, TVP7002_L_FRAME_STAT_LSBS, &lpf_lsb, &error);
	tvp7002_read_err(sd, TVP7002_L_FRAME_STAT_MSBS, &lpf_msb, &error);

	if (error < 0)
		return error;

	tvp7002_read_err(sd, TVP7002_CLK_L_STAT_LSBS, &cpl_lsb, &error);
	tvp7002_read_err(sd, TVP7002_CLK_L_STAT_MSBS, &cpl_msb, &error);

	if (error < 0)
		return error;

	/* Get lines per frame, clocks per line and interlaced/progresive */
	lpfr = lpf_lsb | ((TVP7002_CL_MASK & lpf_msb) << TVP7002_CL_SHIFT);
	cpln = cpl_lsb | ((TVP7002_CL_MASK & cpl_msb) << TVP7002_CL_SHIFT);
	progressive = (lpf_msb & TVP7002_INPR_MASK) >> TVP7002_IP_SHIFT;

	/* Do checking of video modes */
	for (index = 0; index < NUM_PRESETS; index++, presets++)
		if (lpfr  == presets->lines_per_frame &&
			progressive == presets->progressive) {
			if (presets->cpl_min == 0xffff)
				break;
			if (cpln >= presets->cpl_min && cpln <= presets->cpl_max)
				break;
		}

	if (index == NUM_PRESETS) {
		v4l2_err(sd, "querystd error, lpf = %x, cpl = %x\n",
								lpfr, cpln);
		return -EINVAL;
	}

	if (v4l_fill_dv_preset_info(presets->preset, &e_preset))
		return -EINVAL;

	/* Set values in found preset */
	qpreset->preset = presets->preset;

	/* Update lines per frame and clocks per line info */
	v4l2_dbg(1, debug, sd, "Current preset: %d %d",
					e_preset.width, e_preset.height);
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
/*
 * tvp7002_g_register() - Get the value of a register
 * @sd: ptr to v4l2_subdev struct
 * @vreg: ptr to v4l2_dbg_register struct
 *
 * Get the value of a TVP7002 decoder device register.
 * Returns zero when successful, -EINVAL if register read fails or
 * access to I2C client fails, -EPERM if the call is not allowed
 * by diabled CAP_SYS_ADMIN.
 */
static int tvp7002_g_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 val;
	int ret;

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = tvp7002_read(sd, reg->reg & 0xff, &val);
	reg->val = val;
	return ret;
}

/*
 * tvp7002_s_register() - set a control
 * @sd: ptr to v4l2_subdev struct
 * @ctrl: ptr to v4l2_control struct
 *
 * Get the value of a TVP7002 decoder device register.
 * Returns zero when successful, -EINVAL if register read fails or
 * -EPERM if call not allowed.
 */
static int tvp7002_s_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return tvp7002_write(sd, reg->reg & 0xff, reg->val & 0xff);
}
#endif

/*
 * tvp7002_enum_fmt() - Enum supported formats
 * @sd: pointer to standard V4L2 sub-device structure
 * @enable: pointer to format struct
 *
 * Enumerate supported formats.
 */

static int tvp7002_enum_fmt(struct v4l2_subdev *sd,
						struct v4l2_fmtdesc *fmtdesc)
{
	/* Check requested format index is within range */
	if (fmtdesc->index < 0 || fmtdesc->index >= NUM_FORMATS)
		return -EINVAL;
	*fmtdesc = tvp7002_fmt_list[fmtdesc->index];

	return 0;
}

/*
 * tvp7002_s_stream() - V4L2 decoder i/f handler for s_stream
 * @sd: pointer to standard V4L2 sub-device structure
 * @enable: streaming enable or disable
 *
 * Sets streaming to enable or disable, if possible.
 */
static int tvp7002_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct tvp7002 *device = to_tvp7002(sd);
	int error = 0;

	if (device->streaming == enable)
		return 0;

	if (enable) {
		/* Set output state on (low impedance means stream on) */
		error = tvp7002_write(sd, TVP7002_MISC_CTL_2, 0x00);
		device->streaming = enable;
	} else {
		/* Set output state off (high impedance means stream off) */
		error = tvp7002_write(sd, TVP7002_MISC_CTL_2, 0x03);
		if (error)
			v4l2_dbg(1, debug, sd, "Unable to stop streaming\n");

		device->streaming = enable;
	}

	return error;
}

/*
 * tvp7002_log_status() - Print information about register settings
 * @sd: ptr to v4l2_subdev struct
 *
 * Log register values of a TVP7002 decoder device.
 * Returns zero or -EINVAL if read operation fails.
 */
static int tvp7002_log_status(struct v4l2_subdev *sd)
{
	const struct tvp7002_preset_definition *presets = tvp7002_presets;
	struct tvp7002 *device = to_tvp7002(sd);
	struct v4l2_dv_enum_preset e_preset;
	struct v4l2_dv_preset detected;
	int i;

	detected.preset = V4L2_DV_INVALID;
	/* Find my current standard*/
	tvp7002_query_dv_preset(sd, &detected);

	/* Print standard related code values */
	for (i = 0; i < NUM_PRESETS; i++, presets++)
		if (presets->preset == detected.preset)
			break;

	if (v4l_fill_dv_preset_info(device->current_preset->preset, &e_preset))
		return -EINVAL;

	v4l2_info(sd, "Selected DV Preset: %s\n", e_preset.name);
	v4l2_info(sd, "   Pixels per line: %u\n", e_preset.width);
	v4l2_info(sd, "   Lines per frame: %u\n\n", e_preset.height);
	if (i == NUM_PRESETS) {
		v4l2_info(sd, "Detected DV Preset: None\n");
	} else {
		if (v4l_fill_dv_preset_info(presets->preset, &e_preset))
			return -EINVAL;
		v4l2_info(sd, "Detected DV Preset: %s\n", e_preset.name);
		v4l2_info(sd, "  Pixels per line: %u\n", e_preset.width);
		v4l2_info(sd, "  Lines per frame: %u\n\n", e_preset.height);
	}
	v4l2_info(sd, "Streaming enabled: %s\n",
					device->streaming ? "yes" : "no");

	/* Print the current value of the gain control */
	v4l2_info(sd, "Gain: %u\n", device->gain);

	return 0;
}

/* V4L2 core operation handlers */
static const struct v4l2_subdev_core_ops tvp7002_core_ops = {
	.g_chip_ident = tvp7002_g_chip_ident,
	.log_status = tvp7002_log_status,
	.g_ctrl = tvp7002_g_ctrl,
	.s_ctrl = tvp7002_s_ctrl,
	.queryctrl = tvp7002_queryctrl,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = tvp7002_g_register,
	.s_register = tvp7002_s_register,
#endif
};

/* Specific video subsystem operation handlers */
static const struct v4l2_subdev_video_ops tvp7002_video_ops = {
	.s_dv_preset = tvp7002_s_dv_preset,
	.query_dv_preset = tvp7002_query_dv_preset,
	.s_stream = tvp7002_s_stream,
	.g_fmt = tvp7002_g_fmt,
	.s_fmt = tvp7002_s_fmt,
	.enum_fmt = tvp7002_enum_fmt,
};

/* V4L2 top level operation handlers */
static const struct v4l2_subdev_ops tvp7002_ops = {
	.core = &tvp7002_core_ops,
	.video = &tvp7002_video_ops,
};

static struct tvp7002 tvp7002_dev = {
	.streaming = 0,

	.pix = {
		.width = 1280,
		.height = 720,
		.pixelformat = V4L2_PIX_FMT_UYVY,
		.field = V4L2_FIELD_NONE,
		.bytesperline = 1280 * 2,
		.sizeimage = 1280 * 2 * 720,
		.colorspace = V4L2_COLORSPACE_REC709,
		},

	.current_preset = tvp7002_presets,
	.gain = 0,
};

/*
 * tvp7002_probe - Probe a TVP7002 device
 * @sd: ptr to v4l2_subdev struct
 * @ctrl: ptr to i2c_device_id struct
 *
 * Initialize the TVP7002 device
 * Returns zero when successful, -EINVAL if register read fails or
 * -EIO if i2c access is not available.
 */
static int tvp7002_probe(struct i2c_client *c, const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct tvp7002 *device;
	struct v4l2_dv_preset preset;
	int polarity_a;
	int polarity_b;
	u8 revision;

	int error;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(c->adapter,
		I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -EIO;

	if (!c->dev.platform_data) {
		v4l_err(c, "No platform data!!\n");
		return -ENODEV;
	}

	device = kmalloc(sizeof(struct tvp7002), GFP_KERNEL);

	if (!device)
		return -ENOMEM;

	*device = tvp7002_dev;
	sd = &device->sd;
	device->pdata = c->dev.platform_data;

	/* Tell v4l2 the device is ready */
	v4l2_i2c_subdev_init(sd, c, &tvp7002_ops);
	v4l_info(c, "tvp7002 found @ 0x%02x (%s)\n",
					c->addr, c->adapter->name);

	error = tvp7002_read(sd, TVP7002_CHIP_REV, &revision);
	if (error < 0)
		goto found_error;

	/* Get revision number */
	v4l2_info(sd, "Rev. %02x detected.\n", revision);
	if (revision != 0x02)
		v4l2_info(sd, "Unknown revision detected.\n");

	/* Initializes TVP7002 to its default values */
	error = tvp7002_write_inittab(sd, tvp7002_init_default);

	if (error < 0)
		goto found_error;

	/* Set polarity information after registers have been set */
	polarity_a = 0x20 | device->pdata->hs_polarity << 5
			| device->pdata->vs_polarity << 2;
	error = tvp7002_write(sd, TVP7002_SYNC_CTL_1, polarity_a);
	if (error < 0)
		goto found_error;

	polarity_b = 0x01  | device->pdata->fid_polarity << 2
			| device->pdata->sog_polarity << 1
			| device->pdata->clk_polarity;
	error = tvp7002_write(sd, TVP7002_MISC_CTL_3, polarity_b);
	if (error < 0)
		goto found_error;

	/* Set registers according to default video mode */
	preset.preset = device->current_preset->preset;
	error = tvp7002_s_dv_preset(sd, &preset);

found_error:
	if (error < 0)
		kfree(device);

	return error;
}

/*
 * tvp7002_remove - Remove TVP7002 device support
 * @c: ptr to i2c_client struct
 *
 * Reset the TVP7002 device
 * Returns zero.
 */
static int tvp7002_remove(struct i2c_client *c)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(c);
	struct tvp7002 *device = to_tvp7002(sd);

	v4l2_dbg(1, debug, sd, "Removing tvp7002 adapter"
				"on address 0x%x\n", c->addr);

	v4l2_device_unregister_subdev(sd);
	kfree(device);
	return 0;
}

/* I2C Device ID table */
static const struct i2c_device_id tvp7002_id[] = {
	{ "tvp7002", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tvp7002_id);

/* I2C driver data */
static struct i2c_driver tvp7002_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = TVP7002_MODULE_NAME,
	},
	.probe = tvp7002_probe,
	.remove = tvp7002_remove,
	.id_table = tvp7002_id,
};

/*
 * tvp7002_init - Initialize driver via I2C interface
 *
 * Register the TVP7002 driver.
 * Return 0 on success or error code on failure.
 */
static int __init tvp7002_init(void)
{
	return i2c_add_driver(&tvp7002_driver);
}

/*
 * tvp7002_exit - Remove driver via I2C interface
 *
 * Unregister the TVP7002 driver.
 * Returns nothing.
 */
static void __exit tvp7002_exit(void)
{
	i2c_del_driver(&tvp7002_driver);
}

module_init(tvp7002_init);
module_exit(tvp7002_exit);

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
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/module.h>
#include <linux/v4l2-dv-timings.h>
#include <media/tvp7002.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include "tvp7002_reg.h"

MODULE_DESCRIPTION("TI TVP7002 Video and Graphics Digitizer driver");
MODULE_AUTHOR("Santiago Nunez-Corrales <santiago.nunez@ridgerun.com>");
MODULE_LICENSE("GPL");

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
static bool debug;
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
	{ TVP7002_RGB_COARSE_CLAMP_CTL, 0x8c, TVP7002_WRITE },
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
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0xa0, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0x02, TVP7002_WRITE },
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
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x80, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0x98, TVP7002_WRITE },
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
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x80, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0xE0, TVP7002_WRITE },
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
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0x20, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0xa0, TVP7002_WRITE },
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
	{ TVP7002_HPLL_FDBK_DIV_LSBS, 0xc0, TVP7002_WRITE },
	{ TVP7002_HPLL_CRTL, 0x98, TVP7002_WRITE },
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

/* Timings definition for handling device operation */
struct tvp7002_timings_definition {
	struct v4l2_dv_timings timings;
	const struct i2c_reg_value *p_settings;
	enum v4l2_colorspace color_space;
	enum v4l2_field scanmode;
	u16 progressive;
	u16 lines_per_frame;
	u16 cpl_min;
	u16 cpl_max;
};

/* Struct list for digital video timings */
static const struct tvp7002_timings_definition tvp7002_timings[] = {
	{
		V4L2_DV_BT_CEA_1280X720P60,
		tvp7002_parms_720P60,
		V4L2_COLORSPACE_REC709,
		V4L2_FIELD_NONE,
		1,
		0x2EE,
		135,
		153
	},
	{
		V4L2_DV_BT_CEA_1920X1080I60,
		tvp7002_parms_1080I60,
		V4L2_COLORSPACE_REC709,
		V4L2_FIELD_INTERLACED,
		0,
		0x465,
		181,
		205
	},
	{
		V4L2_DV_BT_CEA_1920X1080I50,
		tvp7002_parms_1080I50,
		V4L2_COLORSPACE_REC709,
		V4L2_FIELD_INTERLACED,
		0,
		0x465,
		217,
		245
	},
	{
		V4L2_DV_BT_CEA_1280X720P50,
		tvp7002_parms_720P50,
		V4L2_COLORSPACE_REC709,
		V4L2_FIELD_NONE,
		1,
		0x2EE,
		163,
		183
	},
	{
		V4L2_DV_BT_CEA_1920X1080P60,
		tvp7002_parms_1080P60,
		V4L2_COLORSPACE_REC709,
		V4L2_FIELD_NONE,
		1,
		0x465,
		90,
		102
	},
	{
		V4L2_DV_BT_CEA_720X480P59_94,
		tvp7002_parms_480P,
		V4L2_COLORSPACE_SMPTE170M,
		V4L2_FIELD_NONE,
		1,
		0x20D,
		0xffff,
		0xffff
	},
	{
		V4L2_DV_BT_CEA_720X576P50,
		tvp7002_parms_576P,
		V4L2_COLORSPACE_SMPTE170M,
		V4L2_FIELD_NONE,
		1,
		0x271,
		0xffff,
		0xffff
	}
};

#define NUM_TIMINGS ARRAY_SIZE(tvp7002_timings)

/* Device definition */
struct tvp7002 {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	const struct tvp7002_config *pdata;

	int ver;
	int streaming;

	const struct tvp7002_timings_definition *current_timings;
	struct media_pad pad;
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

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct tvp7002, hdl)->sd;
}

/*
 * tvp7002_read - Read a value from a register in an TVP7002
 * @sd: ptr to v4l2_subdev struct
 * @addr: TVP7002 register address
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
 * @err: pointer to error value
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
 * @err: pointer to error value
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

static int tvp7002_s_dv_timings(struct v4l2_subdev *sd,
					struct v4l2_dv_timings *dv_timings)
{
	struct tvp7002 *device = to_tvp7002(sd);
	const struct v4l2_bt_timings *bt = &dv_timings->bt;
	int i;

	if (dv_timings->type != V4L2_DV_BT_656_1120)
		return -EINVAL;
	for (i = 0; i < NUM_TIMINGS; i++) {
		const struct v4l2_bt_timings *t = &tvp7002_timings[i].timings.bt;

		if (!memcmp(bt, t, &bt->standards - &bt->width)) {
			device->current_timings = &tvp7002_timings[i];
			return tvp7002_write_inittab(sd, tvp7002_timings[i].p_settings);
		}
	}
	return -EINVAL;
}

static int tvp7002_g_dv_timings(struct v4l2_subdev *sd,
					struct v4l2_dv_timings *dv_timings)
{
	struct tvp7002 *device = to_tvp7002(sd);

	*dv_timings = device->current_timings->timings;
	return 0;
}

/*
 * tvp7002_s_ctrl() - Set a control
 * @ctrl: ptr to v4l2_ctrl struct
 *
 * Set a control in TVP7002 decoder device.
 * Returns zero when successful or -EINVAL if register access fails.
 */
static int tvp7002_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	int error = 0;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		tvp7002_write_err(sd, TVP7002_R_FINE_GAIN, ctrl->val, &error);
		tvp7002_write_err(sd, TVP7002_G_FINE_GAIN, ctrl->val, &error);
		tvp7002_write_err(sd, TVP7002_B_FINE_GAIN, ctrl->val, &error);
		return error;
	}
	return -EINVAL;
}

/*
 * tvp7002_mbus_fmt() - V4L2 decoder interface handler for try/s/g_mbus_fmt
 * @sd: pointer to standard V4L2 sub-device structure
 * @f: pointer to mediabus format structure
 *
 * Negotiate the image capture size and mediabus format.
 * There is only one possible format, so this single function works for
 * get, set and try.
 */
static int tvp7002_mbus_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *f)
{
	struct tvp7002 *device = to_tvp7002(sd);
	const struct v4l2_bt_timings *bt = &device->current_timings->timings.bt;

	f->width = bt->width;
	f->height = bt->height;
	f->code = V4L2_MBUS_FMT_YUYV10_1X20;
	f->field = device->current_timings->scanmode;
	f->colorspace = device->current_timings->color_space;

	v4l2_dbg(1, debug, sd, "MBUS_FMT: Width - %d, Height - %d",
			f->width, f->height);
	return 0;
}

/*
 * tvp7002_query_dv() - query DV timings
 * @sd: pointer to standard V4L2 sub-device structure
 * @index: index into the tvp7002_timings array
 *
 * Returns the current DV timings detected by TVP7002. If no active input is
 * detected, returns -EINVAL
 */
static int tvp7002_query_dv(struct v4l2_subdev *sd, int *index)
{
	const struct tvp7002_timings_definition *timings = tvp7002_timings;
	u8 progressive;
	u32 lpfr;
	u32 cpln;
	int error = 0;
	u8 lpf_lsb;
	u8 lpf_msb;
	u8 cpl_lsb;
	u8 cpl_msb;

	/* Return invalid index if no active input is detected */
	*index = NUM_TIMINGS;

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
	for (*index = 0; *index < NUM_TIMINGS; (*index)++, timings++)
		if (lpfr == timings->lines_per_frame &&
			progressive == timings->progressive) {
			if (timings->cpl_min == 0xffff)
				break;
			if (cpln >= timings->cpl_min && cpln <= timings->cpl_max)
				break;
		}

	if (*index == NUM_TIMINGS) {
		v4l2_dbg(1, debug, sd, "detection failed: lpf = %x, cpl = %x\n",
								lpfr, cpln);
		return -ENOLINK;
	}

	/* Update lines per frame and clocks per line info */
	v4l2_dbg(1, debug, sd, "detected timings: %d\n", *index);
	return 0;
}

static int tvp7002_query_dv_timings(struct v4l2_subdev *sd,
					struct v4l2_dv_timings *timings)
{
	int index;
	int err = tvp7002_query_dv(sd, &index);

	if (err)
		return err;
	*timings = tvp7002_timings[index].timings;
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
/*
 * tvp7002_g_register() - Get the value of a register
 * @sd: ptr to v4l2_subdev struct
 * @reg: ptr to v4l2_dbg_register struct
 *
 * Get the value of a TVP7002 decoder device register.
 * Returns zero when successful, -EINVAL if register read fails or
 * access to I2C client fails.
 */
static int tvp7002_g_register(struct v4l2_subdev *sd,
						struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 val;
	int ret;

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;

	ret = tvp7002_read(sd, reg->reg & 0xff, &val);
	reg->val = val;
	return ret;
}

/*
 * tvp7002_s_register() - set a control
 * @sd: ptr to v4l2_subdev struct
 * @reg: ptr to v4l2_dbg_register struct
 *
 * Get the value of a TVP7002 decoder device register.
 * Returns zero when successful, -EINVAL if register read fails.
 */
static int tvp7002_s_register(struct v4l2_subdev *sd,
						const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;

	return tvp7002_write(sd, reg->reg & 0xff, reg->val & 0xff);
}
#endif

/*
 * tvp7002_enum_mbus_fmt() - Enum supported mediabus formats
 * @sd: pointer to standard V4L2 sub-device structure
 * @index: format index
 * @code: pointer to mediabus format
 *
 * Enumerate supported mediabus formats.
 */

static int tvp7002_enum_mbus_fmt(struct v4l2_subdev *sd, unsigned index,
					enum v4l2_mbus_pixelcode *code)
{
	/* Check requested format index is within range */
	if (index)
		return -EINVAL;
	*code = V4L2_MBUS_FMT_YUYV10_1X20;
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
	struct tvp7002 *device = to_tvp7002(sd);
	const struct v4l2_bt_timings *bt;
	int detected;

	/* Find my current timings */
	tvp7002_query_dv(sd, &detected);

	bt = &device->current_timings->timings.bt;
	v4l2_info(sd, "Selected DV Timings: %ux%u\n", bt->width, bt->height);
	if (detected == NUM_TIMINGS) {
		v4l2_info(sd, "Detected DV Timings: None\n");
	} else {
		bt = &tvp7002_timings[detected].timings.bt;
		v4l2_info(sd, "Detected DV Timings: %ux%u\n",
				bt->width, bt->height);
	}
	v4l2_info(sd, "Streaming enabled: %s\n",
					device->streaming ? "yes" : "no");

	/* Print the current value of the gain control */
	v4l2_ctrl_handler_log_status(&device->hdl, sd->name);

	return 0;
}

static int tvp7002_enum_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_enum_dv_timings *timings)
{
	/* Check requested format index is within range */
	if (timings->index >= NUM_TIMINGS)
		return -EINVAL;

	timings->timings = tvp7002_timings[timings->index].timings;
	return 0;
}

static const struct v4l2_ctrl_ops tvp7002_ctrl_ops = {
	.s_ctrl = tvp7002_s_ctrl,
};

/*
 * tvp7002_enum_mbus_code() - Enum supported digital video format on pad
 * @sd: pointer to standard V4L2 sub-device structure
 * @fh: file handle for the subdev
 * @code: pointer to subdev enum mbus code struct
 *
 * Enumerate supported digital video formats for pad.
 */
static int
tvp7002_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_mbus_code_enum *code)
{
	/* Check requested format index is within range */
	if (code->index != 0)
		return -EINVAL;

	code->code = V4L2_MBUS_FMT_YUYV10_1X20;

	return 0;
}

/*
 * tvp7002_get_pad_format() - get video format on pad
 * @sd: pointer to standard V4L2 sub-device structure
 * @fh: file handle for the subdev
 * @fmt: pointer to subdev format struct
 *
 * get video format for pad.
 */
static int
tvp7002_get_pad_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_format *fmt)
{
	struct tvp7002 *tvp7002 = to_tvp7002(sd);

	fmt->format.code = V4L2_MBUS_FMT_YUYV10_1X20;
	fmt->format.width = tvp7002->current_timings->timings.bt.width;
	fmt->format.height = tvp7002->current_timings->timings.bt.height;
	fmt->format.field = tvp7002->current_timings->scanmode;
	fmt->format.colorspace = tvp7002->current_timings->color_space;

	return 0;
}

/*
 * tvp7002_set_pad_format() - set video format on pad
 * @sd: pointer to standard V4L2 sub-device structure
 * @fh: file handle for the subdev
 * @fmt: pointer to subdev format struct
 *
 * set video format for pad.
 */
static int
tvp7002_set_pad_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_format *fmt)
{
	return tvp7002_get_pad_format(sd, fh, fmt);
}

/* V4L2 core operation handlers */
static const struct v4l2_subdev_core_ops tvp7002_core_ops = {
	.g_chip_ident = tvp7002_g_chip_ident,
	.log_status = tvp7002_log_status,
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = tvp7002_g_register,
	.s_register = tvp7002_s_register,
#endif
};

/* Specific video subsystem operation handlers */
static const struct v4l2_subdev_video_ops tvp7002_video_ops = {
	.g_dv_timings = tvp7002_g_dv_timings,
	.s_dv_timings = tvp7002_s_dv_timings,
	.enum_dv_timings = tvp7002_enum_dv_timings,
	.query_dv_timings = tvp7002_query_dv_timings,
	.s_stream = tvp7002_s_stream,
	.g_mbus_fmt = tvp7002_mbus_fmt,
	.try_mbus_fmt = tvp7002_mbus_fmt,
	.s_mbus_fmt = tvp7002_mbus_fmt,
	.enum_mbus_fmt = tvp7002_enum_mbus_fmt,
};

/* media pad related operation handlers */
static const struct v4l2_subdev_pad_ops tvp7002_pad_ops = {
	.enum_mbus_code = tvp7002_enum_mbus_code,
	.get_fmt = tvp7002_get_pad_format,
	.set_fmt = tvp7002_set_pad_format,
};

/* V4L2 top level operation handlers */
static const struct v4l2_subdev_ops tvp7002_ops = {
	.core = &tvp7002_core_ops,
	.video = &tvp7002_video_ops,
	.pad = &tvp7002_pad_ops,
};

/*
 * tvp7002_probe - Probe a TVP7002 device
 * @c: ptr to i2c_client struct
 * @id: ptr to i2c_device_id struct
 *
 * Initialize the TVP7002 device
 * Returns zero when successful, -EINVAL if register read fails or
 * -EIO if i2c access is not available.
 */
static int tvp7002_probe(struct i2c_client *c, const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct tvp7002 *device;
	struct v4l2_dv_timings timings;
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

	device = devm_kzalloc(&c->dev, sizeof(struct tvp7002), GFP_KERNEL);

	if (!device)
		return -ENOMEM;

	sd = &device->sd;
	device->pdata = c->dev.platform_data;
	device->current_timings = tvp7002_timings;

	/* Tell v4l2 the device is ready */
	v4l2_i2c_subdev_init(sd, c, &tvp7002_ops);
	v4l_info(c, "tvp7002 found @ 0x%02x (%s)\n",
					c->addr, c->adapter->name);

	error = tvp7002_read(sd, TVP7002_CHIP_REV, &revision);
	if (error < 0)
		return error;

	/* Get revision number */
	v4l2_info(sd, "Rev. %02x detected.\n", revision);
	if (revision != 0x02)
		v4l2_info(sd, "Unknown revision detected.\n");

	/* Initializes TVP7002 to its default values */
	error = tvp7002_write_inittab(sd, tvp7002_init_default);

	if (error < 0)
		return error;

	/* Set polarity information after registers have been set */
	polarity_a = 0x20 | device->pdata->hs_polarity << 5
			| device->pdata->vs_polarity << 2;
	error = tvp7002_write(sd, TVP7002_SYNC_CTL_1, polarity_a);
	if (error < 0)
		return error;

	polarity_b = 0x01  | device->pdata->fid_polarity << 2
			| device->pdata->sog_polarity << 1
			| device->pdata->clk_polarity;
	error = tvp7002_write(sd, TVP7002_MISC_CTL_3, polarity_b);
	if (error < 0)
		return error;

	/* Set registers according to default video mode */
	timings = device->current_timings->timings;
	error = tvp7002_s_dv_timings(sd, &timings);

#if defined(CONFIG_MEDIA_CONTROLLER)
	strlcpy(sd->name, TVP7002_MODULE_NAME, sizeof(sd->name));
	device->pad.flags = MEDIA_PAD_FL_SOURCE;
	device->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	device->sd.entity.flags |= MEDIA_ENT_T_V4L2_SUBDEV_DECODER;

	error = media_entity_init(&device->sd.entity, 1, &device->pad, 0);
	if (error < 0)
		return error;
#endif

	v4l2_ctrl_handler_init(&device->hdl, 1);
	v4l2_ctrl_new_std(&device->hdl, &tvp7002_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 0);
	sd->ctrl_handler = &device->hdl;
	if (device->hdl.error) {
		error = device->hdl.error;
		goto error;
	}
	v4l2_ctrl_handler_setup(&device->hdl);

	return 0;

error:
	v4l2_ctrl_handler_free(&device->hdl);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&device->sd.entity);
#endif
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
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&device->sd.entity);
#endif
	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&device->hdl);
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

module_i2c_driver(tvp7002_driver);

// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the GalaxyCore GC0308 camera sensor.
 *
 * Copyright (c) 2023 Sebastian Reichel <sre@kernel.org>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Analog & CISCTL*/
#define GC0308_CHIP_ID			CCI_REG8(0x000)
#define GC0308_HBLANK			CCI_REG8(0x001)
#define GC0308_VBLANK			CCI_REG8(0x002)
#define GC0308_EXP			CCI_REG16(0x003)
#define GC0308_ROW_START		CCI_REG16(0x005)
#define GC0308_COL_START		CCI_REG16(0x007)
#define GC0308_WIN_HEIGHT		CCI_REG16(0x009)
#define GC0308_WIN_WIDTH		CCI_REG16(0x00b)
#define GC0308_VS_START_TIME		CCI_REG8(0x00d) /* in rows */
#define GC0308_VS_END_TIME		CCI_REG8(0x00e) /* in rows */
#define GC0308_VB_HB			CCI_REG8(0x00f)
#define GC0308_RSH_WIDTH		CCI_REG8(0x010)
#define GC0308_TSP_WIDTH		CCI_REG8(0x011)
#define GC0308_SAMPLE_HOLD_DELAY	CCI_REG8(0x012)
#define GC0308_ROW_TAIL_WIDTH		CCI_REG8(0x013)
#define GC0308_CISCTL_MODE1		CCI_REG8(0x014)
#define GC0308_CISCTL_MODE2		CCI_REG8(0x015)
#define GC0308_CISCTL_MODE3		CCI_REG8(0x016)
#define GC0308_CISCTL_MODE4		CCI_REG8(0x017)
#define GC0308_ANALOG_MODE1		CCI_REG8(0x01a)
#define GC0308_ANALOG_MODE2		CCI_REG8(0x01b)
#define GC0308_HRST_RSG_V18		CCI_REG8(0x01c)
#define GC0308_VREF_V25			CCI_REG8(0x01d)
#define GC0308_ADC_R			CCI_REG8(0x01e)
#define GC0308_PAD_DRV			CCI_REG8(0x01f)
#define GC0308_SOFT_RESET		CCI_REG8(0x0fe)

/* ISP */
#define GC0308_BLOCK_EN1		CCI_REG8(0x020)
#define GC0308_BLOCK_EN2		CCI_REG8(0x021)
#define GC0308_AAAA_EN			CCI_REG8(0x022)
#define GC0308_SPECIAL_EFFECT		CCI_REG8(0x023)
#define GC0308_OUT_FORMAT		CCI_REG8(0x024)
#define GC0308_OUT_EN			CCI_REG8(0x025)
#define GC0308_SYNC_MODE		CCI_REG8(0x026)
#define GC0308_CLK_DIV_MODE		CCI_REG8(0x028)
#define GC0308_BYPASS_MODE		CCI_REG8(0x029)
#define GC0308_CLK_GATING		CCI_REG8(0x02a)
#define GC0308_DITHER_MODE		CCI_REG8(0x02b)
#define GC0308_DITHER_BIT		CCI_REG8(0x02c)
#define GC0308_DEBUG_MODE1		CCI_REG8(0x02d)
#define GC0308_DEBUG_MODE2		CCI_REG8(0x02e)
#define GC0308_DEBUG_MODE3		CCI_REG8(0x02f)
#define GC0308_CROP_WIN_MODE		CCI_REG8(0x046)
#define GC0308_CROP_WIN_Y1		CCI_REG8(0x047)
#define GC0308_CROP_WIN_X1		CCI_REG8(0x048)
#define GC0308_CROP_WIN_HEIGHT		CCI_REG16(0x049)
#define GC0308_CROP_WIN_WIDTH		CCI_REG16(0x04b)

/* BLK */
#define GC0308_BLK_MODE			CCI_REG8(0x030)
#define GC0308_BLK_LIMIT_VAL		CCI_REG8(0x031)
#define GC0308_GLOBAL_OFF		CCI_REG8(0x032)
#define GC0308_CURRENT_R_OFF		CCI_REG8(0x033)
#define GC0308_CURRENT_G_OFF		CCI_REG8(0x034)
#define GC0308_CURRENT_B_OFF		CCI_REG8(0x035)
#define GC0308_CURRENT_R_DARK_CURRENT	CCI_REG8(0x036)
#define GC0308_CURRENT_G_DARK_CURRENT	CCI_REG8(0x037)
#define GC0308_CURRENT_B_DARK_CURRENT	CCI_REG8(0x038)
#define GC0308_EXP_RATE_DARKC		CCI_REG8(0x039)
#define GC0308_OFF_SUBMODE		CCI_REG8(0x03a)
#define GC0308_DARKC_SUBMODE		CCI_REG8(0x03b)
#define GC0308_MANUAL_G1_OFF		CCI_REG8(0x03c)
#define GC0308_MANUAL_R1_OFF		CCI_REG8(0x03d)
#define GC0308_MANUAL_B2_OFF		CCI_REG8(0x03e)
#define GC0308_MANUAL_G2_OFF		CCI_REG8(0x03f)

/* PREGAIN */
#define GC0308_GLOBAL_GAIN		CCI_REG8(0x050)
#define GC0308_AUTO_PREGAIN		CCI_REG8(0x051)
#define GC0308_AUTO_POSTGAIN		CCI_REG8(0x052)
#define GC0308_CHANNEL_GAIN_G1		CCI_REG8(0x053)
#define GC0308_CHANNEL_GAIN_R		CCI_REG8(0x054)
#define GC0308_CHANNEL_GAIN_B		CCI_REG8(0x055)
#define GC0308_CHANNEL_GAIN_G2		CCI_REG8(0x056)
#define GC0308_R_RATIO			CCI_REG8(0x057)
#define GC0308_G_RATIO			CCI_REG8(0x058)
#define GC0308_B_RATIO			CCI_REG8(0x059)
#define GC0308_AWB_R_GAIN		CCI_REG8(0x05a)
#define GC0308_AWB_G_GAIN		CCI_REG8(0x05b)
#define GC0308_AWB_B_GAIN		CCI_REG8(0x05c)
#define GC0308_LSC_DEC_LVL1		CCI_REG8(0x05d)
#define GC0308_LSC_DEC_LVL2		CCI_REG8(0x05e)
#define GC0308_LSC_DEC_LVL3		CCI_REG8(0x05f)

/* DNDD */
#define GC0308_DN_MODE_EN		CCI_REG8(0x060)
#define GC0308_DN_MODE_RATIO		CCI_REG8(0x061)
#define GC0308_DN_BILAT_B_BASE		CCI_REG8(0x062)
#define GC0308_DN_B_INCR		CCI_REG8(0x063)
#define GC0308_DN_BILAT_N_BASE		CCI_REG8(0x064)
#define GC0308_DN_N_INCR		CCI_REG8(0x065)
#define GC0308_DD_DARK_BRIGHT_TH	CCI_REG8(0x066)
#define GC0308_DD_FLAT_TH		CCI_REG8(0x067)
#define GC0308_DD_LIMIT			CCI_REG8(0x068)

/* ASDE - Auto Saturation De-noise and Edge-Enhancement */
#define GC0308_ASDE_GAIN_TRESH		CCI_REG8(0x069)
#define GC0308_ASDE_GAIN_MODE		CCI_REG8(0x06a)
#define GC0308_ASDE_DN_SLOPE		CCI_REG8(0x06b)
#define GC0308_ASDE_DD_BRIGHT		CCI_REG8(0x06c)
#define GC0308_ASDE_DD_LIMIT		CCI_REG8(0x06d)
#define GC0308_ASDE_AUTO_EE1		CCI_REG8(0x06e)
#define GC0308_ASDE_AUTO_EE2		CCI_REG8(0x06f)
#define GC0308_ASDE_AUTO_SAT_DEC_SLOPE	CCI_REG8(0x070)
#define GC0308_ASDE_AUTO_SAT_LOW_LIMIT	CCI_REG8(0x071)

/* INTPEE - Interpolation and Edge-Enhancement */
#define GC0308_EEINTP_MODE_1		CCI_REG8(0x072)
#define GC0308_EEINTP_MODE_2		CCI_REG8(0x073)
#define GC0308_DIRECTION_TH1		CCI_REG8(0x074)
#define GC0308_DIRECTION_TH2		CCI_REG8(0x075)
#define GC0308_DIFF_HV_TI_TH		CCI_REG8(0x076)
#define GC0308_EDGE12_EFFECT		CCI_REG8(0x077)
#define GC0308_EDGE_POS_RATIO		CCI_REG8(0x078)
#define GC0308_EDGE1_MINMAX		CCI_REG8(0x079)
#define GC0308_EDGE2_MINMAX		CCI_REG8(0x07a)
#define GC0308_EDGE12_TH		CCI_REG8(0x07b)
#define GC0308_EDGE_MAX			CCI_REG8(0x07c)

/* ABB - Auto Black Balance */
#define GC0308_ABB_MODE			CCI_REG8(0x080)
#define GC0308_ABB_TARGET_AVGH		CCI_REG8(0x081)
#define GC0308_ABB_TARGET_AVGL		CCI_REG8(0x082)
#define GC0308_ABB_LIMIT_VAL		CCI_REG8(0x083)
#define GC0308_ABB_SPEED		CCI_REG8(0x084)
#define GC0308_CURR_R_BLACK_LVL		CCI_REG8(0x085)
#define GC0308_CURR_G_BLACK_LVL		CCI_REG8(0x086)
#define GC0308_CURR_B_BLACK_LVL		CCI_REG8(0x087)
#define GC0308_CURR_R_BLACK_FACTOR	CCI_REG8(0x088)
#define GC0308_CURR_G_BLACK_FACTOR	CCI_REG8(0x089)
#define GC0308_CURR_B_BLACK_FACTOR	CCI_REG8(0x08a)

/* LSC - Lens Shading Correction */
#define GC0308_LSC_RED_B2		CCI_REG8(0x08b)
#define GC0308_LSC_GREEN_B2		CCI_REG8(0x08c)
#define GC0308_LSC_BLUE_B2		CCI_REG8(0x08d)
#define GC0308_LSC_RED_B4		CCI_REG8(0x08e)
#define GC0308_LSC_GREEN_B4		CCI_REG8(0x08f)
#define GC0308_LSC_BLUE_B4		CCI_REG8(0x090)
#define GC0308_LSC_ROW_CENTER		CCI_REG8(0x091)
#define GC0308_LSC_COL_CENTER		CCI_REG8(0x092)

/* CC - Channel Coefficient */
#define GC0308_CC_MATRIX_C11		CCI_REG8(0x093)
#define GC0308_CC_MATRIX_C12		CCI_REG8(0x094)
#define GC0308_CC_MATRIX_C13		CCI_REG8(0x095)
#define GC0308_CC_MATRIX_C21		CCI_REG8(0x096)
#define GC0308_CC_MATRIX_C22		CCI_REG8(0x097)
#define GC0308_CC_MATRIX_C23		CCI_REG8(0x098)
#define GC0308_CC_MATRIX_C41		CCI_REG8(0x09c)
#define GC0308_CC_MATRIX_C42		CCI_REG8(0x09d)
#define GC0308_CC_MATRIX_C43		CCI_REG8(0x09e)

/* GAMMA */
#define GC0308_GAMMA_OUT0		CCI_REG8(0x09f)
#define GC0308_GAMMA_OUT1		CCI_REG8(0x0a0)
#define GC0308_GAMMA_OUT2		CCI_REG8(0x0a1)
#define GC0308_GAMMA_OUT3		CCI_REG8(0x0a2)
#define GC0308_GAMMA_OUT4		CCI_REG8(0x0a3)
#define GC0308_GAMMA_OUT5		CCI_REG8(0x0a4)
#define GC0308_GAMMA_OUT6		CCI_REG8(0x0a5)
#define GC0308_GAMMA_OUT7		CCI_REG8(0x0a6)
#define GC0308_GAMMA_OUT8		CCI_REG8(0x0a7)
#define GC0308_GAMMA_OUT9		CCI_REG8(0x0a8)
#define GC0308_GAMMA_OUT10		CCI_REG8(0x0a9)
#define GC0308_GAMMA_OUT11		CCI_REG8(0x0aa)
#define GC0308_GAMMA_OUT12		CCI_REG8(0x0ab)
#define GC0308_GAMMA_OUT13		CCI_REG8(0x0ac)
#define GC0308_GAMMA_OUT14		CCI_REG8(0x0ad)
#define GC0308_GAMMA_OUT15		CCI_REG8(0x0ae)
#define GC0308_GAMMA_OUT16		CCI_REG8(0x0af)

/* YCP */
#define GC0308_GLOBAL_SATURATION	CCI_REG8(0x0b0)
#define GC0308_SATURATION_CB		CCI_REG8(0x0b1)
#define GC0308_SATURATION_CR		CCI_REG8(0x0b2)
#define GC0308_LUMA_CONTRAST		CCI_REG8(0x0b3)
#define GC0308_CONTRAST_CENTER		CCI_REG8(0x0b4)
#define GC0308_LUMA_OFFSET		CCI_REG8(0x0b5)
#define GC0308_SKIN_CB_CENTER		CCI_REG8(0x0b6)
#define GC0308_SKIN_CR_CENTER		CCI_REG8(0x0b7)
#define GC0308_SKIN_RADIUS_SQUARE	CCI_REG8(0x0b8)
#define GC0308_SKIN_BRIGHTNESS		CCI_REG8(0x0b9)
#define GC0308_FIXED_CB			CCI_REG8(0x0ba)
#define GC0308_FIXED_CR			CCI_REG8(0x0bb)
#define GC0308_EDGE_DEC_SA		CCI_REG8(0x0bd)
#define GC0308_AUTO_GRAY_MODE		CCI_REG8(0x0be)
#define GC0308_SATURATION_SUB_STRENGTH	CCI_REG8(0x0bf)
#define GC0308_Y_GAMMA_OUT0		CCI_REG8(0x0c0)
#define GC0308_Y_GAMMA_OUT1		CCI_REG8(0x0c1)
#define GC0308_Y_GAMMA_OUT2		CCI_REG8(0x0c2)
#define GC0308_Y_GAMMA_OUT3		CCI_REG8(0x0c3)
#define GC0308_Y_GAMMA_OUT4		CCI_REG8(0x0c4)
#define GC0308_Y_GAMMA_OUT5		CCI_REG8(0x0c5)
#define GC0308_Y_GAMMA_OUT6		CCI_REG8(0x0c6)
#define GC0308_Y_GAMMA_OUT7		CCI_REG8(0x0c7)
#define GC0308_Y_GAMMA_OUT8		CCI_REG8(0x0c8)
#define GC0308_Y_GAMMA_OUT9		CCI_REG8(0x0c9)
#define GC0308_Y_GAMMA_OUT10		CCI_REG8(0x0ca)
#define GC0308_Y_GAMMA_OUT11		CCI_REG8(0x0cb)
#define GC0308_Y_GAMMA_OUT12		CCI_REG8(0x0cc)

/* AEC - Automatic Exposure Control */
#define GC0308_AEC_MODE1		CCI_REG8(0x0d0)
#define GC0308_AEC_MODE2		CCI_REG8(0x0d1)
#define GC0308_AEC_MODE3		CCI_REG8(0x0d2)
#define GC0308_AEC_TARGET_Y		CCI_REG8(0x0d3)
#define GC0308_Y_AVG			CCI_REG8(0x0d4)
#define GC0308_AEC_HIGH_LOW_RANGE	CCI_REG8(0x0d5)
#define GC0308_AEC_IGNORE		CCI_REG8(0x0d6)
#define GC0308_AEC_LIMIT_HIGH_RANGE	CCI_REG8(0x0d7)
#define GC0308_AEC_R_OFFSET		CCI_REG8(0x0d9)
#define GC0308_AEC_GB_OFFSET		CCI_REG8(0x0da)
#define GC0308_AEC_SLOW_MARGIN		CCI_REG8(0x0db)
#define GC0308_AEC_FAST_MARGIN		CCI_REG8(0x0dc)
#define GC0308_AEC_EXP_CHANGE_GAIN	CCI_REG8(0x0dd)
#define GC0308_AEC_STEP2_SUNLIGHT	CCI_REG8(0x0de)
#define GC0308_AEC_I_FRAMES		CCI_REG8(0x0df)
#define GC0308_AEC_I_STOP_L_MARGIN	CCI_REG8(0x0e0)
#define GC0308_AEC_I_STOP_MARGIN	CCI_REG8(0x0e1)
#define GC0308_ANTI_FLICKER_STEP	CCI_REG16(0x0e2)
#define GC0308_EXP_LVL_1		CCI_REG16(0x0e4)
#define GC0308_EXP_LVL_2		CCI_REG16(0x0e6)
#define GC0308_EXP_LVL_3		CCI_REG16(0x0e8)
#define GC0308_EXP_LVL_4		CCI_REG16(0x0ea)
#define GC0308_MAX_EXP_LVL		CCI_REG8(0x0ec)
#define GC0308_EXP_MIN_L		CCI_REG8(0x0ed)
#define GC0308_MAX_POST_DF_GAIN		CCI_REG8(0x0ee)
#define GC0308_MAX_PRE_DG_GAIN		CCI_REG8(0x0ef)

/* ABS */
#define GC0308_ABS_RANGE_COMP		CCI_REG8(0x0f0)
#define GC0308_ABS_STOP_MARGIN		CCI_REG8(0x0f1)
#define GC0308_Y_S_COMP			CCI_REG8(0x0f2)
#define GC0308_Y_STRETCH_LIMIT		CCI_REG8(0x0f3)
#define GC0308_Y_TILT			CCI_REG8(0x0f4)
#define GC0308_Y_STRETCH		CCI_REG8(0x0f5)

/* Measure Window */
#define GC0308_BIG_WIN_X0		CCI_REG8(0x0f7)
#define GC0308_BIG_WIN_Y0		CCI_REG8(0x0f8)
#define GC0308_BIG_WIN_X1		CCI_REG8(0x0f9)
#define GC0308_BIG_WIN_Y1		CCI_REG8(0x0fa)
#define GC0308_DIFF_Y_BIG_THD		CCI_REG8(0x0fb)

/* OUT Module (P1) */
#define GC0308_CLOSE_FRAME_EN		CCI_REG8(0x150)
#define GC0308_CLOSE_FRAME_NUM1		CCI_REG8(0x151)
#define GC0308_CLOSE_FRAME_NUM2		CCI_REG8(0x152)
#define GC0308_BAYER_MODE		CCI_REG8(0x153)
#define GC0308_SUBSAMPLE		CCI_REG8(0x154)
#define GC0308_SUBMODE			CCI_REG8(0x155)
#define GC0308_SUB_ROW_N1		CCI_REG8(0x156)
#define GC0308_SUB_ROW_N2		CCI_REG8(0x157)
#define GC0308_SUB_COL_N1		CCI_REG8(0x158)
#define GC0308_SUB_COL_N2		CCI_REG8(0x159)

/* AWB (P1) - Auto White Balance */
#define GC0308_AWB_RGB_HIGH_LOW		CCI_REG8(0x100)
#define GC0308_AWB_Y_TO_C_DIFF2		CCI_REG8(0x102)
#define GC0308_AWB_C_MAX		CCI_REG8(0x104)
#define GC0308_AWB_C_INTER		CCI_REG8(0x105)
#define GC0308_AWB_C_INTER2		CCI_REG8(0x106)
#define GC0308_AWB_C_MAX_BIG		CCI_REG8(0x108)
#define GC0308_AWB_Y_HIGH		CCI_REG8(0x109)
#define GC0308_AWB_NUMBER_LIMIT		CCI_REG8(0x10a)
#define GC0308_KWIN_RATIO		CCI_REG8(0x10b)
#define GC0308_KWIN_THD			CCI_REG8(0x10c)
#define GC0308_LIGHT_GAIN_RANGE		CCI_REG8(0x10d)
#define GC0308_SMALL_WIN_WIDTH_STEP	CCI_REG8(0x10e)
#define GC0308_SMALL_WIN_HEIGHT_STEP	CCI_REG8(0x10f)
#define GC0308_AWB_YELLOW_TH		CCI_REG8(0x110)
#define GC0308_AWB_MODE			CCI_REG8(0x111)
#define GC0308_AWB_ADJUST_SPEED		CCI_REG8(0x112)
#define GC0308_AWB_EVERY_N		CCI_REG8(0x113)
#define GC0308_R_AVG_USE		CCI_REG8(0x1d0)
#define GC0308_G_AVG_USE		CCI_REG8(0x1d1)
#define GC0308_B_AVG_USE		CCI_REG8(0x1d2)

#define GC0308_HBLANK_MIN		0x021
#define GC0308_HBLANK_MAX		0xfff
#define GC0308_HBLANK_DEF		0x040

#define GC0308_VBLANK_MIN		0x000
#define GC0308_VBLANK_MAX		0xfff
#define GC0308_VBLANK_DEF		0x020

#define GC0308_PIXEL_RATE		24000000

/*
 * frame_time = (BT + height + 8) * row_time
 * width = 640 (driver does not change window size)
 * height = 480 (driver does not change window size)
 * row_time = HBLANK + SAMPLE_HOLD_DELAY + width + 8 + 4
 *
 * When EXP_TIME > (BT + height):
 *     BT = EXP_TIME - height - 8 - VS_START_TIME + VS_END_TIME
 * else:
 *     BT = VBLANK + VS_START_TIME + VS_END_TIME
 *
 * max is 30 FPS
 *
 * In my tests frame rate mostly depends on exposure time. Unfortuantely
 * it's unclear how this is calculated exactly. Also since we enable AEC,
 * the frame times vary depending on ambient light conditions.
 */
#define GC0308_FRAME_RATE_MAX		30

enum gc0308_exp_val {
	GC0308_EXP_M4 = 0,
	GC0308_EXP_M3,
	GC0308_EXP_M2,
	GC0308_EXP_M1,
	GC0308_EXP_0,
	GC0308_EXP_P1,
	GC0308_EXP_P2,
	GC0308_EXP_P3,
	GC0308_EXP_P4,
};

static const s64 gc0308_exposure_menu[] = {
	-4, -3, -2, -1, 0, 1, 2, 3, 4
};

struct gc0308_exposure {
	u8 luma_offset;
	u8 aec_target_y;
};

#define GC0308_EXPOSURE(luma_offset_reg, aec_target_y_reg) \
	{ .luma_offset = luma_offset_reg, .aec_target_y = aec_target_y_reg }

static const struct gc0308_exposure gc0308_exposure_values[] = {
	[GC0308_EXP_M4] = GC0308_EXPOSURE(0xc0, 0x30),
	[GC0308_EXP_M3] = GC0308_EXPOSURE(0xd0, 0x38),
	[GC0308_EXP_M2] = GC0308_EXPOSURE(0xe0, 0x40),
	[GC0308_EXP_M1] = GC0308_EXPOSURE(0xf0, 0x48),
	[GC0308_EXP_0]  = GC0308_EXPOSURE(0x08, 0x50),
	[GC0308_EXP_P1] = GC0308_EXPOSURE(0x10, 0x5c),
	[GC0308_EXP_P2] = GC0308_EXPOSURE(0x20, 0x60),
	[GC0308_EXP_P3] = GC0308_EXPOSURE(0x30, 0x68),
	[GC0308_EXP_P4] = GC0308_EXPOSURE(0x40, 0x70),
};

struct gc0308_awb_gains {
	u8 r;
	u8 g;
	u8 b;
};

#define GC0308_AWB_GAINS(red, green, blue) \
	{ .r = red, .g = green, .b = blue }

static const struct gc0308_awb_gains gc0308_awb_gains[] = {
	[V4L2_WHITE_BALANCE_AUTO]         = GC0308_AWB_GAINS(0x56, 0x40, 0x4a),
	[V4L2_WHITE_BALANCE_CLOUDY]       = GC0308_AWB_GAINS(0x8c, 0x50, 0x40),
	[V4L2_WHITE_BALANCE_DAYLIGHT]     = GC0308_AWB_GAINS(0x74, 0x52, 0x40),
	[V4L2_WHITE_BALANCE_INCANDESCENT] = GC0308_AWB_GAINS(0x48, 0x40, 0x5c),
	[V4L2_WHITE_BALANCE_FLUORESCENT]  = GC0308_AWB_GAINS(0x40, 0x42, 0x50),
};

struct gc0308_format {
	u32 code;
	u8 regval;
};

#define GC0308_FORMAT(v4l2_code, gc0308_regval) \
	{ .code = v4l2_code, .regval = gc0308_regval }

static const struct gc0308_format gc0308_formats[] = {
	GC0308_FORMAT(MEDIA_BUS_FMT_UYVY8_2X8, 0x00),
	GC0308_FORMAT(MEDIA_BUS_FMT_VYUY8_2X8, 0x01),
	GC0308_FORMAT(MEDIA_BUS_FMT_YUYV8_2X8, 0x02),
	GC0308_FORMAT(MEDIA_BUS_FMT_YVYU8_2X8, 0x03),
	GC0308_FORMAT(MEDIA_BUS_FMT_RGB565_2X8_BE, 0x06),
	GC0308_FORMAT(MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE, 0x07),
	GC0308_FORMAT(MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE, 0x09),
};

struct gc0308_frame_size {
	u8 subsample;
	u32 width;
	u32 height;
};

#define GC0308_FRAME_SIZE(s, w, h) \
	{ .subsample = s, .width = w, .height = h }

static const struct gc0308_frame_size gc0308_frame_sizes[] = {
	GC0308_FRAME_SIZE(0x11, 640, 480),
	GC0308_FRAME_SIZE(0x22, 320, 240),
	GC0308_FRAME_SIZE(0x44, 160, 120),
};

struct gc0308_mode_registers {
	u8 out_format;
	u8 subsample;
	u16 width;
	u16 height;
};

struct gc0308 {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct media_pad pad;
	struct device *dev;
	struct clk *clk;
	struct regmap *regmap;
	struct regulator *vdd;
	struct gpio_desc *pwdn_gpio;
	struct gpio_desc *reset_gpio;
	unsigned int mbus_config;
	struct gc0308_mode_registers mode;
	struct {
		/* mirror cluster */
		struct v4l2_ctrl *hflip;
		struct v4l2_ctrl *vflip;
	};
	struct {
		/* blanking cluster */
		struct v4l2_ctrl *hblank;
		struct v4l2_ctrl *vblank;
	};
};

static inline struct gc0308 *to_gc0308(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc0308, sd);
}

static const struct regmap_range_cfg gc0308_ranges[] = {
	{
		.range_min	= 0x0000,
		.range_max	= 0x01ff,
		.selector_reg	= 0xfe,
		.selector_mask	= 0x01,
		.selector_shift	= 0x00,
		.window_start	= 0x00,
		.window_len	= 0x100,
	},
};

static const struct regmap_config gc0308_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = 0x1ff,
	.ranges = gc0308_ranges,
	.num_ranges = ARRAY_SIZE(gc0308_ranges),
	.disable_locking = true,
};

static const struct cci_reg_sequence sensor_default_regs[] = {
	{GC0308_VB_HB, 0x00},
	{GC0308_HBLANK, 0x40},
	{GC0308_VBLANK, 0x20},
	{GC0308_EXP, 0x0258},
	{GC0308_AWB_R_GAIN, 0x56},
	{GC0308_AWB_G_GAIN, 0x40},
	{GC0308_AWB_B_GAIN, 0x4a},
	{GC0308_ANTI_FLICKER_STEP, 0x0078},
	{GC0308_EXP_LVL_1, 0x0258},
	{GC0308_EXP_LVL_2, 0x0258},
	{GC0308_EXP_LVL_3, 0x0258},
	{GC0308_EXP_LVL_4, 0x0ea6},
	{GC0308_MAX_EXP_LVL, 0x20},
	{GC0308_ROW_START, 0x0000},
	{GC0308_COL_START, 0x0000},
	{GC0308_WIN_HEIGHT, 488},
	{GC0308_WIN_WIDTH, 648},
	{GC0308_VS_START_TIME, 0x02},
	{GC0308_VS_END_TIME, 0x02},
	{GC0308_RSH_WIDTH, 0x22},
	{GC0308_TSP_WIDTH, 0x0d},
	{GC0308_SAMPLE_HOLD_DELAY, 0x50},
	{GC0308_ROW_TAIL_WIDTH, 0x0f},
	{GC0308_CISCTL_MODE1, 0x10},
	{GC0308_CISCTL_MODE2, 0x0a},
	{GC0308_CISCTL_MODE3, 0x05},
	{GC0308_CISCTL_MODE4, 0x01},
	{CCI_REG8(0x018), 0x44}, /* undocumented */
	{CCI_REG8(0x019), 0x44}, /* undocumented */
	{GC0308_ANALOG_MODE1, 0x2a},
	{GC0308_ANALOG_MODE2, 0x00},
	{GC0308_HRST_RSG_V18, 0x49},
	{GC0308_VREF_V25, 0x9a},
	{GC0308_ADC_R, 0x61},
	{GC0308_PAD_DRV, 0x01}, /* drv strength: pclk=4mA */
	{GC0308_BLOCK_EN1, 0x7f},
	{GC0308_BLOCK_EN2, 0xfa},
	{GC0308_AAAA_EN, 0x57},
	{GC0308_OUT_FORMAT, 0xa2}, /* YCbYCr */
	{GC0308_OUT_EN, 0x0f},
	{GC0308_SYNC_MODE, 0x03},
	{GC0308_CLK_DIV_MODE, 0x00},
	{GC0308_DEBUG_MODE1, 0x0a},
	{GC0308_DEBUG_MODE2, 0x00},
	{GC0308_DEBUG_MODE3, 0x01},
	{GC0308_BLK_MODE, 0xf7},
	{GC0308_BLK_LIMIT_VAL, 0x50},
	{GC0308_GLOBAL_OFF, 0x00},
	{GC0308_CURRENT_R_OFF, 0x28},
	{GC0308_CURRENT_G_OFF, 0x2a},
	{GC0308_CURRENT_B_OFF, 0x28},
	{GC0308_EXP_RATE_DARKC, 0x04},
	{GC0308_OFF_SUBMODE, 0x20},
	{GC0308_DARKC_SUBMODE, 0x20},
	{GC0308_MANUAL_G1_OFF, 0x00},
	{GC0308_MANUAL_R1_OFF, 0x00},
	{GC0308_MANUAL_B2_OFF, 0x00},
	{GC0308_MANUAL_G2_OFF, 0x00},
	{GC0308_GLOBAL_GAIN, 0x14},
	{GC0308_AUTO_POSTGAIN, 0x41},
	{GC0308_CHANNEL_GAIN_G1, 0x80},
	{GC0308_CHANNEL_GAIN_R, 0x80},
	{GC0308_CHANNEL_GAIN_B, 0x80},
	{GC0308_CHANNEL_GAIN_G2, 0x80},
	{GC0308_LSC_RED_B2, 0x20},
	{GC0308_LSC_GREEN_B2, 0x20},
	{GC0308_LSC_BLUE_B2, 0x20},
	{GC0308_LSC_RED_B4, 0x14},
	{GC0308_LSC_GREEN_B4, 0x10},
	{GC0308_LSC_BLUE_B4, 0x14},
	{GC0308_LSC_ROW_CENTER, 0x3c},
	{GC0308_LSC_COL_CENTER, 0x50},
	{GC0308_LSC_DEC_LVL1, 0x12},
	{GC0308_LSC_DEC_LVL2, 0x1a},
	{GC0308_LSC_DEC_LVL3, 0x24},
	{GC0308_DN_MODE_EN, 0x07},
	{GC0308_DN_MODE_RATIO, 0x15},
	{GC0308_DN_BILAT_B_BASE, 0x08},
	{GC0308_DN_BILAT_N_BASE, 0x03},
	{GC0308_DD_DARK_BRIGHT_TH, 0xe8},
	{GC0308_DD_FLAT_TH, 0x86},
	{GC0308_DD_LIMIT, 0x82},
	{GC0308_ASDE_GAIN_TRESH, 0x18},
	{GC0308_ASDE_GAIN_MODE, 0x0f},
	{GC0308_ASDE_DN_SLOPE, 0x00},
	{GC0308_ASDE_DD_BRIGHT, 0x5f},
	{GC0308_ASDE_DD_LIMIT, 0x8f},
	{GC0308_ASDE_AUTO_EE1, 0x55},
	{GC0308_ASDE_AUTO_EE2, 0x38},
	{GC0308_ASDE_AUTO_SAT_DEC_SLOPE, 0x15},
	{GC0308_ASDE_AUTO_SAT_LOW_LIMIT, 0x33},
	{GC0308_EEINTP_MODE_1, 0xdc},
	{GC0308_EEINTP_MODE_2, 0x00},
	{GC0308_DIRECTION_TH1, 0x02},
	{GC0308_DIRECTION_TH2, 0x3f},
	{GC0308_DIFF_HV_TI_TH, 0x02},
	{GC0308_EDGE12_EFFECT, 0x38},
	{GC0308_EDGE_POS_RATIO, 0x88},
	{GC0308_EDGE1_MINMAX, 0x81},
	{GC0308_EDGE2_MINMAX, 0x81},
	{GC0308_EDGE12_TH, 0x22},
	{GC0308_EDGE_MAX, 0xff},
	{GC0308_CC_MATRIX_C11, 0x48},
	{GC0308_CC_MATRIX_C12, 0x02},
	{GC0308_CC_MATRIX_C13, 0x07},
	{GC0308_CC_MATRIX_C21, 0xe0},
	{GC0308_CC_MATRIX_C22, 0x40},
	{GC0308_CC_MATRIX_C23, 0xf0},
	{GC0308_SATURATION_CB, 0x40},
	{GC0308_SATURATION_CR, 0x40},
	{GC0308_LUMA_CONTRAST, 0x40},
	{GC0308_SKIN_CB_CENTER, 0xe0},
	{GC0308_EDGE_DEC_SA, 0x38},
	{GC0308_AUTO_GRAY_MODE, 0x36},
	{GC0308_AEC_MODE1, 0xcb},
	{GC0308_AEC_MODE2, 0x10},
	{GC0308_AEC_MODE3, 0x90},
	{GC0308_AEC_TARGET_Y, 0x48},
	{GC0308_AEC_HIGH_LOW_RANGE, 0xf2},
	{GC0308_AEC_IGNORE, 0x16},
	{GC0308_AEC_SLOW_MARGIN, 0x92},
	{GC0308_AEC_FAST_MARGIN, 0xa5},
	{GC0308_AEC_I_FRAMES, 0x23},
	{GC0308_AEC_R_OFFSET, 0x00},
	{GC0308_AEC_GB_OFFSET, 0x00},
	{GC0308_AEC_I_STOP_L_MARGIN, 0x09},
	{GC0308_EXP_MIN_L, 0x04},
	{GC0308_MAX_POST_DF_GAIN, 0xa0},
	{GC0308_MAX_PRE_DG_GAIN, 0x40},
	{GC0308_ABB_MODE, 0x03},
	{GC0308_GAMMA_OUT0, 0x10},
	{GC0308_GAMMA_OUT1, 0x20},
	{GC0308_GAMMA_OUT2, 0x38},
	{GC0308_GAMMA_OUT3, 0x4e},
	{GC0308_GAMMA_OUT4, 0x63},
	{GC0308_GAMMA_OUT5, 0x76},
	{GC0308_GAMMA_OUT6, 0x87},
	{GC0308_GAMMA_OUT7, 0xa2},
	{GC0308_GAMMA_OUT8, 0xb8},
	{GC0308_GAMMA_OUT9, 0xca},
	{GC0308_GAMMA_OUT10, 0xd8},
	{GC0308_GAMMA_OUT11, 0xe3},
	{GC0308_GAMMA_OUT12, 0xeb},
	{GC0308_GAMMA_OUT13, 0xf0},
	{GC0308_GAMMA_OUT14, 0xf8},
	{GC0308_GAMMA_OUT15, 0xfd},
	{GC0308_GAMMA_OUT16, 0xff},
	{GC0308_Y_GAMMA_OUT0, 0x00},
	{GC0308_Y_GAMMA_OUT1, 0x10},
	{GC0308_Y_GAMMA_OUT2, 0x1c},
	{GC0308_Y_GAMMA_OUT3, 0x30},
	{GC0308_Y_GAMMA_OUT4, 0x43},
	{GC0308_Y_GAMMA_OUT5, 0x54},
	{GC0308_Y_GAMMA_OUT6, 0x65},
	{GC0308_Y_GAMMA_OUT7, 0x75},
	{GC0308_Y_GAMMA_OUT8, 0x93},
	{GC0308_Y_GAMMA_OUT9, 0xb0},
	{GC0308_Y_GAMMA_OUT10, 0xcb},
	{GC0308_Y_GAMMA_OUT11, 0xe6},
	{GC0308_Y_GAMMA_OUT12, 0xff},
	{GC0308_ABS_RANGE_COMP, 0x02},
	{GC0308_ABS_STOP_MARGIN, 0x01},
	{GC0308_Y_S_COMP, 0x02},
	{GC0308_Y_STRETCH_LIMIT, 0x30},
	{GC0308_BIG_WIN_X0, 0x12},
	{GC0308_BIG_WIN_Y0, 0x0a},
	{GC0308_BIG_WIN_X1, 0x9f},
	{GC0308_BIG_WIN_Y1, 0x78},
	{GC0308_AWB_RGB_HIGH_LOW, 0xf5},
	{GC0308_AWB_Y_TO_C_DIFF2, 0x20},
	{GC0308_AWB_C_MAX, 0x10},
	{GC0308_AWB_C_INTER, 0x08},
	{GC0308_AWB_C_INTER2, 0x20},
	{GC0308_AWB_C_MAX_BIG, 0x0a},
	{GC0308_AWB_NUMBER_LIMIT, 0xa0},
	{GC0308_KWIN_RATIO, 0x60},
	{GC0308_KWIN_THD, 0x08},
	{GC0308_SMALL_WIN_WIDTH_STEP, 0x44},
	{GC0308_SMALL_WIN_HEIGHT_STEP, 0x32},
	{GC0308_AWB_YELLOW_TH, 0x41},
	{GC0308_AWB_MODE, 0x37},
	{GC0308_AWB_ADJUST_SPEED, 0x22},
	{GC0308_AWB_EVERY_N, 0x19},
	{CCI_REG8(0x114), 0x44}, /* AWB set1 */
	{CCI_REG8(0x115), 0x44}, /* AWB set1 */
	{CCI_REG8(0x116), 0xc2}, /* AWB set1 */
	{CCI_REG8(0x117), 0xa8}, /* AWB set1 */
	{CCI_REG8(0x118), 0x18}, /* AWB set1 */
	{CCI_REG8(0x119), 0x50}, /* AWB set1 */
	{CCI_REG8(0x11a), 0xd8}, /* AWB set1 */
	{CCI_REG8(0x11b), 0xf5}, /* AWB set1 */
	{CCI_REG8(0x170), 0x40}, /* AWB set2 */
	{CCI_REG8(0x171), 0x58}, /* AWB set2 */
	{CCI_REG8(0x172), 0x30}, /* AWB set2 */
	{CCI_REG8(0x173), 0x48}, /* AWB set2 */
	{CCI_REG8(0x174), 0x20}, /* AWB set2 */
	{CCI_REG8(0x175), 0x60}, /* AWB set2 */
	{CCI_REG8(0x177), 0x20}, /* AWB set2 */
	{CCI_REG8(0x178), 0x32}, /* AWB set2 */
	{CCI_REG8(0x130), 0x03}, /* undocumented */
	{CCI_REG8(0x131), 0x40}, /* undocumented */
	{CCI_REG8(0x132), 0x10}, /* undocumented */
	{CCI_REG8(0x133), 0xe0}, /* undocumented */
	{CCI_REG8(0x134), 0xe0}, /* undocumented */
	{CCI_REG8(0x135), 0x00}, /* undocumented */
	{CCI_REG8(0x136), 0x80}, /* undocumented */
	{CCI_REG8(0x137), 0x00}, /* undocumented */
	{CCI_REG8(0x138), 0x04}, /* undocumented */
	{CCI_REG8(0x139), 0x09}, /* undocumented */
	{CCI_REG8(0x13a), 0x12}, /* undocumented */
	{CCI_REG8(0x13b), 0x1c}, /* undocumented */
	{CCI_REG8(0x13c), 0x28}, /* undocumented */
	{CCI_REG8(0x13d), 0x31}, /* undocumented */
	{CCI_REG8(0x13e), 0x44}, /* undocumented */
	{CCI_REG8(0x13f), 0x57}, /* undocumented */
	{CCI_REG8(0x140), 0x6c}, /* undocumented */
	{CCI_REG8(0x141), 0x81}, /* undocumented */
	{CCI_REG8(0x142), 0x94}, /* undocumented */
	{CCI_REG8(0x143), 0xa7}, /* undocumented */
	{CCI_REG8(0x144), 0xb8}, /* undocumented */
	{CCI_REG8(0x145), 0xd6}, /* undocumented */
	{CCI_REG8(0x146), 0xee}, /* undocumented */
	{CCI_REG8(0x147), 0x0d}, /* undocumented */
	{CCI_REG8(0x162), 0xf7}, /* undocumented */
	{CCI_REG8(0x163), 0x68}, /* undocumented */
	{CCI_REG8(0x164), 0xd3}, /* undocumented */
	{CCI_REG8(0x165), 0xd3}, /* undocumented */
	{CCI_REG8(0x166), 0x60}, /* undocumented */
};

struct gc0308_colormode {
	u8 special_effect;
	u8 dbg_mode1;
	u8 block_en1;
	u8 aec_mode3;
	u8 eeintp_mode_2;
	u8 edge12_effect;
	u8 luma_contrast;
	u8 contrast_center;
	u8 fixed_cb;
	u8 fixed_cr;
};

#define GC0308_COLOR_FX(reg_special_effect, reg_dbg_mode1, reg_block_en1, \
			reg_aec_mode3, reg_eeintp_mode_2, reg_edge12_effect, \
			reg_luma_contrast, reg_contrast_center, \
			reg_fixed_cb, reg_fixed_cr) \
	{ \
		.special_effect = reg_special_effect, \
		.dbg_mode1 = reg_dbg_mode1, \
		.block_en1 = reg_block_en1, \
		.aec_mode3 = reg_aec_mode3, \
		.eeintp_mode_2 = reg_eeintp_mode_2, \
		.edge12_effect = reg_edge12_effect, \
		.luma_contrast = reg_luma_contrast, \
		.contrast_center = reg_contrast_center, \
		.fixed_cb = reg_fixed_cb, \
		.fixed_cr = reg_fixed_cr, \
	}

static const struct gc0308_colormode gc0308_colormodes[] = {
	[V4L2_COLORFX_NONE] =
		GC0308_COLOR_FX(0x00, 0x0a, 0xff, 0x90, 0x00,
				0x54, 0x3c, 0x80, 0x00, 0x00),
	[V4L2_COLORFX_BW] =
		GC0308_COLOR_FX(0x02, 0x0a, 0xff, 0x90, 0x00,
				0x54, 0x40, 0x80, 0x00, 0x00),
	[V4L2_COLORFX_SEPIA] =
		GC0308_COLOR_FX(0x02, 0x0a, 0xff, 0x90, 0x00,
				0x38, 0x40, 0x80, 0xd0, 0x28),
	[V4L2_COLORFX_NEGATIVE] =
		GC0308_COLOR_FX(0x01, 0x0a, 0xff, 0x90, 0x00,
				0x38, 0x40, 0x80, 0x00, 0x00),
	[V4L2_COLORFX_EMBOSS] =
		GC0308_COLOR_FX(0x02, 0x0a, 0xbf, 0x10, 0x01,
				0x38, 0x40, 0x80, 0x00, 0x00),
	[V4L2_COLORFX_SKETCH] =
		GC0308_COLOR_FX(0x02, 0x0a, 0xff, 0x10, 0x80,
				0x38, 0x80, 0x90, 0x00, 0x00),
	[V4L2_COLORFX_SKY_BLUE] =
		GC0308_COLOR_FX(0x02, 0x0a, 0xff, 0x90, 0x00,
				0x38, 0x40, 0x80, 0x50, 0xe0),
	[V4L2_COLORFX_GRASS_GREEN] =
		GC0308_COLOR_FX(0x02, 0x0a, 0xff, 0x90, 0x01,
				0x38, 0x40, 0x80, 0xc0, 0xc0),
	[V4L2_COLORFX_SKIN_WHITEN] =
		GC0308_COLOR_FX(0x02, 0x0a, 0xbf, 0x10, 0x01,
				0x38, 0x60, 0x40, 0x00, 0x00),
};

static int gc0308_power_on(struct device *dev)
{
	struct gc0308 *gc0308 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(gc0308->vdd);
	if (ret)
		return ret;

	ret = clk_prepare_enable(gc0308->clk);
	if (ret)
		goto clk_fail;

	gpiod_set_value_cansleep(gc0308->pwdn_gpio, 0);
	usleep_range(10000, 20000);

	gpiod_set_value_cansleep(gc0308->reset_gpio, 1);
	usleep_range(10000, 20000);
	gpiod_set_value_cansleep(gc0308->reset_gpio, 0);
	msleep(30);

	return 0;

clk_fail:
	regulator_disable(gc0308->vdd);
	return ret;
}

static int gc0308_power_off(struct device *dev)
{
	struct gc0308 *gc0308 = dev_get_drvdata(dev);

	gpiod_set_value_cansleep(gc0308->pwdn_gpio, 1);
	clk_disable_unprepare(gc0308->clk);
	regulator_disable(gc0308->vdd);

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int gc0308_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct gc0308 *gc0308 = to_gc0308(sd);

	return cci_read(gc0308->regmap, CCI_REG8(reg->reg), &reg->val, NULL);
}

static int gc0308_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct gc0308 *gc0308 = to_gc0308(sd);

	return cci_write(gc0308->regmap, CCI_REG8(reg->reg), reg->val, NULL);
}
#endif

static int gc0308_set_exposure(struct gc0308 *gc0308, enum gc0308_exp_val exp)
{
	const struct gc0308_exposure *regs = &gc0308_exposure_values[exp];
	struct cci_reg_sequence exposure_reg_seq[] = {
		{GC0308_LUMA_OFFSET, regs->luma_offset},
		{GC0308_AEC_TARGET_Y, regs->aec_target_y},
	};

	return cci_multi_reg_write(gc0308->regmap, exposure_reg_seq,
				   ARRAY_SIZE(exposure_reg_seq), NULL);
}

static int gc0308_set_awb_mode(struct gc0308 *gc0308,
			       enum v4l2_auto_n_preset_white_balance val)
{
	const struct gc0308_awb_gains *regs = &gc0308_awb_gains[val];
	struct cci_reg_sequence awb_reg_seq[] = {
		{GC0308_AWB_R_GAIN, regs->r},
		{GC0308_AWB_G_GAIN, regs->g},
		{GC0308_AWB_B_GAIN, regs->b},
	};
	int ret;

	ret = cci_update_bits(gc0308->regmap, GC0308_AAAA_EN,
			      BIT(1), val == V4L2_WHITE_BALANCE_AUTO, NULL);
	ret = cci_multi_reg_write(gc0308->regmap, awb_reg_seq,
				  ARRAY_SIZE(awb_reg_seq), &ret);

	return ret;
}

static int gc0308_set_colormode(struct gc0308 *gc0308, enum v4l2_colorfx mode)
{
	const struct gc0308_colormode *regs = &gc0308_colormodes[mode];
	struct cci_reg_sequence colormode_reg_seq[] = {
		{GC0308_SPECIAL_EFFECT, regs->special_effect},
		{GC0308_DEBUG_MODE1, regs->dbg_mode1},
		{GC0308_BLOCK_EN1, regs->block_en1},
		{GC0308_AEC_MODE3, regs->aec_mode3},
		{GC0308_EEINTP_MODE_2, regs->eeintp_mode_2},
		{GC0308_EDGE12_EFFECT, regs->edge12_effect},
		{GC0308_LUMA_CONTRAST, regs->luma_contrast},
		{GC0308_CONTRAST_CENTER, regs->contrast_center},
		{GC0308_FIXED_CB, regs->fixed_cb},
		{GC0308_FIXED_CR, regs->fixed_cr},
	};

	return cci_multi_reg_write(gc0308->regmap, colormode_reg_seq,
				   ARRAY_SIZE(colormode_reg_seq), NULL);
}

static int gc0308_set_power_line_freq(struct gc0308 *gc0308, int frequency)
{
	static const struct cci_reg_sequence pwr_line_50hz[] = {
		{GC0308_ANTI_FLICKER_STEP, 0x0078},
		{GC0308_EXP_LVL_1, 0x0258},
		{GC0308_EXP_LVL_2, 0x0348},
		{GC0308_EXP_LVL_3, 0x04b0},
		{GC0308_EXP_LVL_4, 0x05a0},
	};
	static const struct cci_reg_sequence pwr_line_60hz[] = {
		{GC0308_ANTI_FLICKER_STEP, 0x0064},
		{GC0308_EXP_LVL_1, 0x0258},
		{GC0308_EXP_LVL_2, 0x0384},
		{GC0308_EXP_LVL_3, 0x04b0},
		{GC0308_EXP_LVL_4, 0x05dc},
	};

	switch (frequency) {
	case V4L2_CID_POWER_LINE_FREQUENCY_60HZ:
		return cci_multi_reg_write(gc0308->regmap, pwr_line_60hz,
					   ARRAY_SIZE(pwr_line_60hz), NULL);
	case V4L2_CID_POWER_LINE_FREQUENCY_50HZ:
		return cci_multi_reg_write(gc0308->regmap, pwr_line_50hz,
					   ARRAY_SIZE(pwr_line_50hz), NULL);
	}

	return -EINVAL;
}

static int gc0308_update_mirror(struct gc0308 *gc0308)
{
	u8 regval = 0x00;

	if (gc0308->vflip->val)
		regval |= BIT(1);

	if (gc0308->hflip->val)
		regval |= BIT(0);

	return cci_update_bits(gc0308->regmap, GC0308_CISCTL_MODE1,
			       GENMASK(1, 0), regval, NULL);
}

static int gc0308_update_blanking(struct gc0308 *gc0308)
{
	u16 vblank = gc0308->vblank->val;
	u16 hblank = gc0308->hblank->val;
	u8 vbhb = ((vblank >> 4) & 0xf0) | ((hblank >> 8) & 0x0f);
	int ret = 0;

	cci_write(gc0308->regmap, GC0308_VB_HB, vbhb, &ret);
	cci_write(gc0308->regmap, GC0308_HBLANK, hblank & 0xff, &ret);
	cci_write(gc0308->regmap, GC0308_VBLANK, vblank & 0xff, &ret);

	return ret;
}

static int _gc0308_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc0308 *gc0308 = container_of(ctrl->handler, struct gc0308, hdl);
	u8 flipval = ctrl->val ? 0xff : 0x00;

	switch (ctrl->id) {
	case V4L2_CID_HBLANK:
	case V4L2_CID_VBLANK:
		return gc0308_update_blanking(gc0308);
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		return gc0308_update_mirror(gc0308);
	case V4L2_CID_AUTO_WHITE_BALANCE:
		return cci_update_bits(gc0308->regmap, GC0308_AAAA_EN,
				       BIT(1), flipval, NULL);
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		return gc0308_set_awb_mode(gc0308, ctrl->val);
	case V4L2_CID_POWER_LINE_FREQUENCY:
		return gc0308_set_power_line_freq(gc0308, ctrl->val);
	case V4L2_CID_COLORFX:
		return gc0308_set_colormode(gc0308, ctrl->val);
	case V4L2_CID_TEST_PATTERN:
		return cci_update_bits(gc0308->regmap, GC0308_DEBUG_MODE2,
				       GENMASK(1, 0), ctrl->val, NULL);
	case V4L2_CID_AUTO_EXPOSURE_BIAS:
		return gc0308_set_exposure(gc0308, ctrl->val);
	}

	return -EINVAL;
}

static int gc0308_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc0308 *gc0308 = container_of(ctrl->handler, struct gc0308, hdl);
	int ret;

	if (!pm_runtime_get_if_in_use(gc0308->dev))
		return 0;

	ret = _gc0308_s_ctrl(ctrl);
	if (ret)
		dev_err(gc0308->dev, "failed to set control: %d\n", ret);

	pm_runtime_mark_last_busy(gc0308->dev);
	pm_runtime_put_autosuspend(gc0308->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc0308_ctrl_ops = {
	.s_ctrl = gc0308_s_ctrl,
};

static const struct v4l2_subdev_core_ops gc0308_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= gc0308_g_register,
	.s_register	= gc0308_s_register,
#endif
};

static int gc0308_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(gc0308_formats))
		return -EINVAL;

	code->code = gc0308_formats[code->index].code;

	return 0;
}

static int gc0308_get_format_idx(u32 code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gc0308_formats); i++) {
		if (gc0308_formats[i].code == code)
			return i;
	}

	return -1;
}

static int gc0308_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(gc0308_frame_sizes))
		return -EINVAL;

	if (gc0308_get_format_idx(fse->code) < 0)
		return -EINVAL;

	fse->min_width = gc0308_frame_sizes[fse->index].width;
	fse->max_width = gc0308_frame_sizes[fse->index].width;
	fse->min_height = gc0308_frame_sizes[fse->index].height;
	fse->max_height = gc0308_frame_sizes[fse->index].height;

	return 0;
}

static void gc0308_update_pad_format(const struct gc0308_frame_size *mode,
				     struct v4l2_mbus_framefmt *fmt, u32 code)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = code;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

static int gc0308_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct gc0308 *gc0308 = to_gc0308(sd);
	const struct gc0308_frame_size *mode;
	int i = gc0308_get_format_idx(fmt->format.code);

	if (i < 0)
		i = 0;

	mode = v4l2_find_nearest_size(gc0308_frame_sizes,
				      ARRAY_SIZE(gc0308_frame_sizes), width,
				      height, fmt->format.width,
				      fmt->format.height);

	gc0308_update_pad_format(mode, &fmt->format, gc0308_formats[i].code);
	*v4l2_subdev_state_get_format(sd_state, 0) = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	gc0308->mode.out_format = gc0308_formats[i].regval;
	gc0308->mode.subsample = mode->subsample;
	gc0308->mode.width = mode->width;
	gc0308->mode.height = mode->height;

	return 0;
}

static int gc0308_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *format =
		v4l2_subdev_state_get_format(sd_state, 0);

	format->width		= 640;
	format->height		= 480;
	format->code		= gc0308_formats[0].code;
	format->colorspace	= V4L2_COLORSPACE_SRGB;
	format->field		= V4L2_FIELD_NONE;
	format->ycbcr_enc	= V4L2_YCBCR_ENC_DEFAULT;
	format->quantization	= V4L2_QUANTIZATION_DEFAULT;
	format->xfer_func	= V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static const struct v4l2_subdev_pad_ops gc0308_pad_ops = {
	.enum_mbus_code = gc0308_enum_mbus_code,
	.enum_frame_size = gc0308_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = gc0308_set_format,
};

static int gc0308_set_resolution(struct gc0308 *gc0308, int *ret)
{
	struct cci_reg_sequence resolution_regs[] = {
		{GC0308_SUBSAMPLE, gc0308->mode.subsample},
		{GC0308_SUBMODE, 0x03},
		{GC0308_SUB_ROW_N1, 0x00},
		{GC0308_SUB_ROW_N2, 0x00},
		{GC0308_SUB_COL_N1, 0x00},
		{GC0308_SUB_COL_N2, 0x00},
		{GC0308_CROP_WIN_MODE, 0x80},
		{GC0308_CROP_WIN_Y1, 0x00},
		{GC0308_CROP_WIN_X1, 0x00},
		{GC0308_CROP_WIN_HEIGHT, gc0308->mode.height},
		{GC0308_CROP_WIN_WIDTH, gc0308->mode.width},
	};

	return cci_multi_reg_write(gc0308->regmap, resolution_regs,
				   ARRAY_SIZE(resolution_regs), ret);
}

static int gc0308_start_stream(struct gc0308 *gc0308)
{
	int ret, sync_mode;

	ret = pm_runtime_resume_and_get(gc0308->dev);
	if (ret < 0)
		return ret;

	cci_multi_reg_write(gc0308->regmap, sensor_default_regs,
			    ARRAY_SIZE(sensor_default_regs), &ret);
	cci_update_bits(gc0308->regmap, GC0308_OUT_FORMAT,
			GENMASK(4, 0), gc0308->mode.out_format, &ret);
	gc0308_set_resolution(gc0308, &ret);

	if (ret) {
		dev_err(gc0308->dev, "failed to update registers: %d\n", ret);
		goto disable_pm;
	}

	ret = __v4l2_ctrl_handler_setup(&gc0308->hdl);
	if (ret) {
		dev_err(gc0308->dev, "failed to setup controls\n");
		goto disable_pm;
	}

	/* HSYNC/VSYNC polarity */
	sync_mode = 0x3;
	if (gc0308->mbus_config & V4L2_MBUS_VSYNC_ACTIVE_LOW)
		sync_mode &= ~BIT(0);
	if (gc0308->mbus_config & V4L2_MBUS_HSYNC_ACTIVE_LOW)
		sync_mode &= ~BIT(1);
	ret = cci_write(gc0308->regmap, GC0308_SYNC_MODE, sync_mode, NULL);
	if (ret)
		goto disable_pm;

	return 0;

disable_pm:
	pm_runtime_mark_last_busy(gc0308->dev);
	pm_runtime_put_autosuspend(gc0308->dev);
	return ret;
}

static int gc0308_stop_stream(struct gc0308 *gc0308)
{
	pm_runtime_mark_last_busy(gc0308->dev);
	pm_runtime_put_autosuspend(gc0308->dev);
	return 0;
}

static int gc0308_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct gc0308 *gc0308 = to_gc0308(sd);
	struct v4l2_subdev_state *sd_state;
	int ret;

	sd_state = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable)
		ret = gc0308_start_stream(gc0308);
	else
		ret = gc0308_stop_stream(gc0308);

	v4l2_subdev_unlock_state(sd_state);
	return ret;
}

static const struct v4l2_subdev_video_ops gc0308_video_ops = {
	.s_stream		= gc0308_s_stream,
};

static const struct v4l2_subdev_ops gc0308_subdev_ops = {
	.core	= &gc0308_core_ops,
	.pad	= &gc0308_pad_ops,
	.video	= &gc0308_video_ops,
};

static const struct v4l2_subdev_internal_ops gc0308_internal_ops = {
	.init_state = gc0308_init_state,
};

static int gc0308_bus_config(struct gc0308 *gc0308)
{
	struct device *dev = gc0308->dev;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_PARALLEL
	};
	struct fwnode_handle *ep;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0, 0);
	if (!ep)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	gc0308->mbus_config = bus_cfg.bus.parallel.flags;

	return 0;
}

static const char * const gc0308_test_pattern_menu[] = {
	"Disabled",
	"Test Image 1",
	"Test Image 2",
};

static int gc0308_init_controls(struct gc0308 *gc0308)
{
	int ret;

	v4l2_ctrl_handler_init(&gc0308->hdl, 11);
	gc0308->hblank = v4l2_ctrl_new_std(&gc0308->hdl, &gc0308_ctrl_ops,
					   V4L2_CID_HBLANK, GC0308_HBLANK_MIN,
					   GC0308_HBLANK_MAX, 1,
					   GC0308_HBLANK_DEF);
	gc0308->vblank = v4l2_ctrl_new_std(&gc0308->hdl, &gc0308_ctrl_ops,
					   V4L2_CID_VBLANK, GC0308_VBLANK_MIN,
					   GC0308_VBLANK_MAX, 1,
					   GC0308_VBLANK_DEF);
	gc0308->hflip = v4l2_ctrl_new_std(&gc0308->hdl, &gc0308_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	gc0308->vflip = v4l2_ctrl_new_std(&gc0308->hdl, &gc0308_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&gc0308->hdl, &gc0308_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  GC0308_PIXEL_RATE, GC0308_PIXEL_RATE, 1,
			  GC0308_PIXEL_RATE);
	v4l2_ctrl_new_std(&gc0308->hdl, &gc0308_ctrl_ops,
			  V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, 1);
	v4l2_ctrl_new_std_menu_items(&gc0308->hdl, &gc0308_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(gc0308_test_pattern_menu) - 1,
				     0, 0, gc0308_test_pattern_menu);
	v4l2_ctrl_new_std_menu(&gc0308->hdl, &gc0308_ctrl_ops,
			       V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,
			       8, ~0x14e, V4L2_WHITE_BALANCE_AUTO);
	v4l2_ctrl_new_std_menu(&gc0308->hdl, &gc0308_ctrl_ops,
			       V4L2_CID_COLORFX, 8, 0, V4L2_COLORFX_NONE);
	v4l2_ctrl_new_std_menu(&gc0308->hdl, &gc0308_ctrl_ops,
			       V4L2_CID_POWER_LINE_FREQUENCY,
			       V4L2_CID_POWER_LINE_FREQUENCY_60HZ,
			       ~0x6, V4L2_CID_POWER_LINE_FREQUENCY_50HZ);
	v4l2_ctrl_new_int_menu(&gc0308->hdl, &gc0308_ctrl_ops,
			       V4L2_CID_AUTO_EXPOSURE_BIAS,
			       ARRAY_SIZE(gc0308_exposure_menu) - 1,
			       ARRAY_SIZE(gc0308_exposure_menu) / 2,
			       gc0308_exposure_menu);

	gc0308->sd.ctrl_handler = &gc0308->hdl;
	if (gc0308->hdl.error) {
		ret = gc0308->hdl.error;
		v4l2_ctrl_handler_free(&gc0308->hdl);
		return ret;
	}

	v4l2_ctrl_cluster(2, &gc0308->hflip);
	v4l2_ctrl_cluster(2, &gc0308->hblank);

	return 0;
}

static int gc0308_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct gc0308 *gc0308;
	unsigned long clkrate;
	u64 regval;
	int ret;

	gc0308 = devm_kzalloc(dev, sizeof(*gc0308), GFP_KERNEL);
	if (!gc0308)
		return -ENOMEM;

	gc0308->dev = dev;
	dev_set_drvdata(dev, gc0308);

	ret = gc0308_bus_config(gc0308);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get bus config\n");

	gc0308->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(gc0308->clk))
		return dev_err_probe(dev, PTR_ERR(gc0308->clk),
				     "could not get clk\n");

	gc0308->vdd = devm_regulator_get(dev, "vdd28");
	if (IS_ERR(gc0308->vdd))
		return dev_err_probe(dev, PTR_ERR(gc0308->vdd),
				     "failed to get vdd28 regulator\n");

	gc0308->pwdn_gpio = devm_gpiod_get(dev, "powerdown", GPIOD_OUT_LOW);
	if (IS_ERR(gc0308->pwdn_gpio))
		return dev_err_probe(dev, PTR_ERR(gc0308->pwdn_gpio),
				     "failed to get powerdown gpio\n");

	gc0308->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc0308->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(gc0308->reset_gpio),
				     "failed to get reset gpio\n");

	/*
	 * This is not using devm_cci_regmap_init_i2c(), because the driver
	 * makes use of regmap's pagination feature. The chosen settings are
	 * compatible with the CCI helpers.
	 */
	gc0308->regmap = devm_regmap_init_i2c(client, &gc0308_regmap_config);
	if (IS_ERR(gc0308->regmap))
		return dev_err_probe(dev, PTR_ERR(gc0308->regmap),
				     "failed to init regmap\n");

	v4l2_i2c_subdev_init(&gc0308->sd, client, &gc0308_subdev_ops);
	gc0308->sd.internal_ops = &gc0308_internal_ops;
	gc0308->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	gc0308->sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS;

	ret = gc0308_init_controls(gc0308);
	if (ret)
		return dev_err_probe(dev, ret, "failed to init controls\n");

	gc0308->sd.state_lock = gc0308->hdl.lock;
	gc0308->pad.flags = MEDIA_PAD_FL_SOURCE;
	gc0308->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&gc0308->sd.entity, 1, &gc0308->pad);
	if (ret < 0)
		goto fail_ctrl_hdl_cleanup;

	ret = v4l2_subdev_init_finalize(&gc0308->sd);
	if (ret)
		goto fail_media_entity_cleanup;

	ret = gc0308_power_on(dev);
	if (ret)
		goto fail_subdev_cleanup;

	if (gc0308->clk) {
		clkrate = clk_get_rate(gc0308->clk);
		if (clkrate != 24000000)
			dev_warn(dev, "unexpected clock rate: %lu\n", clkrate);
	}

	ret = cci_read(gc0308->regmap, GC0308_CHIP_ID, &regval, NULL);
	if (ret < 0) {
		dev_err_probe(dev, ret, "failed to read chip ID\n");
		goto fail_power_off;
	}

	if (regval != 0x9b) {
		ret = -EINVAL;
		dev_err_probe(dev, ret, "invalid chip ID (%02llx)\n", regval);
		goto fail_power_off;
	}

	/*
	 * Enable runtime PM with autosuspend. As the device has been powered
	 * manually, mark it as active, and increase the usage count without
	 * resuming the device.
	 */
	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	ret = v4l2_async_register_subdev(&gc0308->sd);
	if (ret) {
		dev_err_probe(dev, ret, "failed to register v4l subdev\n");
		goto fail_rpm;
	}

	return 0;

fail_rpm:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
fail_power_off:
	gc0308_power_off(dev);
fail_subdev_cleanup:
	v4l2_subdev_cleanup(&gc0308->sd);
fail_media_entity_cleanup:
	media_entity_cleanup(&gc0308->sd.entity);
fail_ctrl_hdl_cleanup:
	v4l2_ctrl_handler_free(&gc0308->hdl);
	return ret;
}

static void gc0308_remove(struct i2c_client *client)
{
	struct gc0308 *gc0308 = i2c_get_clientdata(client);
	struct device *dev = &client->dev;

	v4l2_async_unregister_subdev(&gc0308->sd);
	v4l2_ctrl_handler_free(&gc0308->hdl);
	media_entity_cleanup(&gc0308->sd.entity);

	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		gc0308_power_off(dev);
	pm_runtime_set_suspended(dev);
}

static const struct dev_pm_ops gc0308_pm_ops = {
	SET_RUNTIME_PM_OPS(gc0308_power_off, gc0308_power_on, NULL)
};

static const struct of_device_id gc0308_of_match[] = {
	{ .compatible = "galaxycore,gc0308" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gc0308_of_match);

static struct i2c_driver gc0308_i2c_driver = {
	.driver = {
		.name  = "gc0308",
		.pm = &gc0308_pm_ops,
		.of_match_table = gc0308_of_match,
	},
	.probe  = gc0308_probe,
	.remove = gc0308_remove,
};
module_i2c_driver(gc0308_i2c_driver);

MODULE_DESCRIPTION("GalaxyCore GC0308 Camera Driver");
MODULE_AUTHOR("Sebastian Reichel <sre@kernel.org>");
MODULE_LICENSE("GPL");

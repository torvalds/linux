// SPDX-License-Identifier: GPL-2.0
/*
 * Intersil ISL7998x analog to MIPI CSI-2 or BT.656 decoder driver.
 *
 * Copyright (C) 2018-2019 Marek Vasut <marex@denx.de>
 * Copyright (C) 2021 Michael Tretter <kernel@pengutronix.de>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>

/*
 * This control allows to activate and deactivate the test pattern on
 * selected output channels.
 * This value is ISL7998x specific.
 */
#define V4L2_CID_TEST_PATTERN_CHANNELS	(V4L2_CID_USER_ISL7998X_BASE + 0)

/*
 * This control allows to specify the color of the test pattern.
 * This value is ISL7998x specific.
 */
#define V4L2_CID_TEST_PATTERN_COLOR	(V4L2_CID_USER_ISL7998X_BASE + 1)

/*
 * This control allows to specify the bar pattern in the test pattern.
 * This value is ISL7998x specific.
 */
#define V4L2_CID_TEST_PATTERN_BARS	(V4L2_CID_USER_ISL7998X_BASE + 2)

#define ISL7998X_INPUTS			4

#define ISL7998X_REG(page, reg)		(((page) << 8) | (reg))

#define ISL7998X_REG_PN_SIZE			256
#define ISL7998X_REG_PN_BASE(n)			((n) * ISL7998X_REG_PN_SIZE)

#define ISL7998X_REG_PX_DEC_PAGE(page)		ISL7998X_REG((page), 0xff)
#define ISL7998X_REG_PX_DEC_PAGE_MASK		0xf
#define ISL7998X_REG_P0_PRODUCT_ID_CODE		ISL7998X_REG(0, 0x00)
#define ISL7998X_REG_P0_PRODUCT_REV_CODE	ISL7998X_REG(0, 0x01)
#define ISL7998X_REG_P0_SW_RESET_CTL		ISL7998X_REG(0, 0x02)
#define ISL7998X_REG_P0_IO_BUFFER_CTL		ISL7998X_REG(0, 0x03)
#define ISL7998X_REG_P0_IO_BUFFER_CTL_1_1	ISL7998X_REG(0, 0x04)
#define ISL7998X_REG_P0_IO_PAD_PULL_EN_CTL	ISL7998X_REG(0, 0x05)
#define ISL7998X_REG_P0_IO_BUFFER_CTL_1_2	ISL7998X_REG(0, 0x06)
#define ISL7998X_REG_P0_VIDEO_IN_CHAN_CTL	ISL7998X_REG(0, 0x07)
#define ISL7998X_REG_P0_CLK_CTL_1		ISL7998X_REG(0, 0x08)
#define ISL7998X_REG_P0_CLK_CTL_2		ISL7998X_REG(0, 0x09)
#define ISL7998X_REG_P0_CLK_CTL_3		ISL7998X_REG(0, 0x0a)
#define ISL7998X_REG_P0_CLK_CTL_4		ISL7998X_REG(0, 0x0b)
#define ISL7998X_REG_P0_MPP1_SYNC_CTL		ISL7998X_REG(0, 0x0c)
#define ISL7998X_REG_P0_MPP2_SYNC_CTL		ISL7998X_REG(0, 0x0d)
#define ISL7998X_REG_P0_IRQ_SYNC_CTL		ISL7998X_REG(0, 0x0e)
#define ISL7998X_REG_P0_INTERRUPT_STATUS	ISL7998X_REG(0, 0x10)
#define ISL7998X_REG_P0_CHAN_1_IRQ		ISL7998X_REG(0, 0x11)
#define ISL7998X_REG_P0_CHAN_2_IRQ		ISL7998X_REG(0, 0x12)
#define ISL7998X_REG_P0_CHAN_3_IRQ		ISL7998X_REG(0, 0x13)
#define ISL7998X_REG_P0_CHAN_4_IRQ		ISL7998X_REG(0, 0x14)
#define ISL7998X_REG_P0_SHORT_DIAG_IRQ		ISL7998X_REG(0, 0x15)
#define ISL7998X_REG_P0_CHAN_1_IRQ_EN		ISL7998X_REG(0, 0x16)
#define ISL7998X_REG_P0_CHAN_2_IRQ_EN		ISL7998X_REG(0, 0x17)
#define ISL7998X_REG_P0_CHAN_3_IRQ_EN		ISL7998X_REG(0, 0x18)
#define ISL7998X_REG_P0_CHAN_4_IRQ_EN		ISL7998X_REG(0, 0x19)
#define ISL7998X_REG_P0_SHORT_DIAG_IRQ_EN	ISL7998X_REG(0, 0x1a)
#define ISL7998X_REG_P0_CHAN_1_STATUS		ISL7998X_REG(0, 0x1b)
#define ISL7998X_REG_P0_CHAN_2_STATUS		ISL7998X_REG(0, 0x1c)
#define ISL7998X_REG_P0_CHAN_3_STATUS		ISL7998X_REG(0, 0x1d)
#define ISL7998X_REG_P0_CHAN_4_STATUS		ISL7998X_REG(0, 0x1e)
#define ISL7998X_REG_P0_SHORT_DIAG_STATUS	ISL7998X_REG(0, 0x1f)
#define ISL7998X_REG_P0_CLOCK_DELAY		ISL7998X_REG(0, 0x20)

#define ISL7998X_REG_PX_DEC_INPUT_FMT(pg)	ISL7998X_REG((pg), 0x02)
#define ISL7998X_REG_PX_DEC_STATUS_1(pg)	ISL7998X_REG((pg), 0x03)
#define ISL7998X_REG_PX_DEC_STATUS_1_VDLOSS	BIT(7)
#define ISL7998X_REG_PX_DEC_STATUS_1_HLOCK	BIT(6)
#define ISL7998X_REG_PX_DEC_STATUS_1_VLOCK	BIT(3)
#define ISL7998X_REG_PX_DEC_HS_DELAY_CTL(pg)	ISL7998X_REG((pg), 0x04)
#define ISL7998X_REG_PX_DEC_ANCTL(pg)		ISL7998X_REG((pg), 0x06)
#define ISL7998X_REG_PX_DEC_CROP_HI(pg)		ISL7998X_REG((pg), 0x07)
#define ISL7998X_REG_PX_DEC_VDELAY_LO(pg)	ISL7998X_REG((pg), 0x08)
#define ISL7998X_REG_PX_DEC_VACTIVE_LO(pg)	ISL7998X_REG((pg), 0x09)
#define ISL7998X_REG_PX_DEC_HDELAY_LO(pg)	ISL7998X_REG((pg), 0x0a)
#define ISL7998X_REG_PX_DEC_HACTIVE_LO(pg)	ISL7998X_REG((pg), 0x0b)
#define ISL7998X_REG_PX_DEC_CNTRL1(pg)		ISL7998X_REG((pg), 0x0c)
#define ISL7998X_REG_PX_DEC_CSC_CTL(pg)		ISL7998X_REG((pg), 0x0d)
#define ISL7998X_REG_PX_DEC_BRIGHT(pg)		ISL7998X_REG((pg), 0x10)
#define ISL7998X_REG_PX_DEC_CONTRAST(pg)	ISL7998X_REG((pg), 0x11)
#define ISL7998X_REG_PX_DEC_SHARPNESS(pg)	ISL7998X_REG((pg), 0x12)
#define ISL7998X_REG_PX_DEC_SAT_U(pg)		ISL7998X_REG((pg), 0x13)
#define ISL7998X_REG_PX_DEC_SAT_V(pg)		ISL7998X_REG((pg), 0x14)
#define ISL7998X_REG_PX_DEC_HUE(pg)		ISL7998X_REG((pg), 0x15)
#define ISL7998X_REG_PX_DEC_VERT_PEAK(pg)	ISL7998X_REG((pg), 0x17)
#define ISL7998X_REG_PX_DEC_CORING(pg)		ISL7998X_REG((pg), 0x18)
#define ISL7998X_REG_PX_DEC_SDT(pg)		ISL7998X_REG((pg), 0x1c)
#define ISL7998X_REG_PX_DEC_SDT_DET		BIT(7)
#define ISL7998X_REG_PX_DEC_SDT_NOW		GENMASK(6, 4)
#define ISL7998X_REG_PX_DEC_SDT_STANDARD	GENMASK(2, 0)
#define ISL7998X_REG_PX_DEC_SDT_STANDARD_NTSC_M		0
#define ISL7998X_REG_PX_DEC_SDT_STANDARD_PAL		1
#define ISL7998X_REG_PX_DEC_SDT_STANDARD_SECAM		2
#define ISL7998X_REG_PX_DEC_SDT_STANDARD_NTSC_443	3
#define ISL7998X_REG_PX_DEC_SDT_STANDARD_PAL_M		4
#define ISL7998X_REG_PX_DEC_SDT_STANDARD_PAL_CN		5
#define ISL7998X_REG_PX_DEC_SDT_STANDARD_PAL_60		6
#define ISL7998X_REG_PX_DEC_SDT_STANDARD_UNKNOWN	7
#define ISL7998X_REG_PX_DEC_SDTR(pg)		ISL7998X_REG((pg), 0x1d)
#define ISL7998X_REG_PX_DEC_SDTR_ATSTART	BIT(7)
#define ISL7998X_REG_PX_DEC_CLMPG(pg)		ISL7998X_REG((pg), 0x20)
#define ISL7998X_REG_PX_DEC_IAGC(pg)		ISL7998X_REG((pg), 0x21)
#define ISL7998X_REG_PX_DEC_AGCGAIN(pg)		ISL7998X_REG((pg), 0x22)
#define ISL7998X_REG_PX_DEC_PEAKWT(pg)		ISL7998X_REG((pg), 0x23)
#define ISL7998X_REG_PX_DEC_CLMPL(pg)		ISL7998X_REG((pg), 0x24)
#define ISL7998X_REG_PX_DEC_SYNCT(pg)		ISL7998X_REG((pg), 0x25)
#define ISL7998X_REG_PX_DEC_MISSCNT(pg)		ISL7998X_REG((pg), 0x26)
#define ISL7998X_REG_PX_DEC_PCLAMP(pg)		ISL7998X_REG((pg), 0x27)
#define ISL7998X_REG_PX_DEC_VERT_CTL_1(pg)	ISL7998X_REG((pg), 0x28)
#define ISL7998X_REG_PX_DEC_VERT_CTL_2(pg)	ISL7998X_REG((pg), 0x29)
#define ISL7998X_REG_PX_DEC_CLR_KILL_LVL(pg)	ISL7998X_REG((pg), 0x2a)
#define ISL7998X_REG_PX_DEC_COMB_FILTER_CTL(pg)	ISL7998X_REG((pg), 0x2b)
#define ISL7998X_REG_PX_DEC_LUMA_DELAY(pg)	ISL7998X_REG((pg), 0x2c)
#define ISL7998X_REG_PX_DEC_MISC1(pg)		ISL7998X_REG((pg), 0x2d)
#define ISL7998X_REG_PX_DEC_MISC2(pg)		ISL7998X_REG((pg), 0x2e)
#define ISL7998X_REG_PX_DEC_MISC3(pg)		ISL7998X_REG((pg), 0x2f)
#define ISL7998X_REG_PX_DEC_MVSN(pg)		ISL7998X_REG((pg), 0x30)
#define ISL7998X_REG_PX_DEC_CSTATUS2(pg)	ISL7998X_REG((pg), 0x31)
#define ISL7998X_REG_PX_DEC_HFREF(pg)		ISL7998X_REG((pg), 0x32)
#define ISL7998X_REG_PX_DEC_CLMD(pg)		ISL7998X_REG((pg), 0x33)
#define ISL7998X_REG_PX_DEC_ID_DET_CTL(pg)	ISL7998X_REG((pg), 0x34)
#define ISL7998X_REG_PX_DEC_CLCNTL(pg)		ISL7998X_REG((pg), 0x35)
#define ISL7998X_REG_PX_DEC_DIFF_CLMP_CTL_1(pg)	ISL7998X_REG((pg), 0x36)
#define ISL7998X_REG_PX_DEC_DIFF_CLMP_CTL_2(pg)	ISL7998X_REG((pg), 0x37)
#define ISL7998X_REG_PX_DEC_DIFF_CLMP_CTL_3(pg)	ISL7998X_REG((pg), 0x38)
#define ISL7998X_REG_PX_DEC_DIFF_CLMP_CTL_4(pg)	ISL7998X_REG((pg), 0x39)
#define ISL7998X_REG_PX_DEC_SHORT_DET_CTL(pg)	ISL7998X_REG((pg), 0x3a)
#define ISL7998X_REG_PX_DEC_SHORT_DET_CTL_1(pg)	ISL7998X_REG((pg), 0x3b)
#define ISL7998X_REG_PX_DEC_AFE_TST_MUX_CTL(pg)	ISL7998X_REG((pg), 0x3c)
#define ISL7998X_REG_PX_DEC_DATA_CONV(pg)	ISL7998X_REG((pg), 0x3d)
#define ISL7998X_REG_PX_DEC_INTERNAL_TEST(pg)	ISL7998X_REG((pg), 0x3f)
#define ISL7998X_REG_PX_DEC_H_DELAY_CTL(pg)	ISL7998X_REG((pg), 0x43)
#define ISL7998X_REG_PX_DEC_H_DELAY_II_HI(pg)	ISL7998X_REG((pg), 0x44)
#define ISL7998X_REG_PX_DEC_H_DELAY_II_LOW(pg)	ISL7998X_REG((pg), 0x45)

#define ISL7998X_REG_PX_ACA_CTL_1(pg)		ISL7998X_REG((pg), 0x80)
#define ISL7998X_REG_PX_ACA_GAIN_CTL(pg)	ISL7998X_REG((pg), 0x81)
#define ISL7998X_REG_PX_ACA_Y_AVG_HI_LIMIT(pg)	ISL7998X_REG((pg), 0x82)
#define ISL7998X_REG_PX_ACA_Y_AVG_LO_LIMIT(pg)	ISL7998X_REG((pg), 0x83)
#define ISL7998X_REG_PX_ACA_Y_DET_THRESHOLD(pg)	ISL7998X_REG((pg), 0x84)
#define ISL7998X_REG_PX_ACA_BLACK_LVL(pg)	ISL7998X_REG((pg), 0x85)
#define ISL7998X_REG_PX_ACA_CENTER_LVL(pg)	ISL7998X_REG((pg), 0x86)
#define ISL7998X_REG_PX_ACA_WHITE_LVL(pg)	ISL7998X_REG((pg), 0x87)
#define ISL7998X_REG_PX_ACA_MEAN_OFF_LIMIT(pg)	ISL7998X_REG((pg), 0x88)
#define ISL7998X_REG_PX_ACA_MEAN_OFF_UPGAIN(pg)	ISL7998X_REG((pg), 0x89)
#define ISL7998X_REG_PX_ACA_MEAN_OFF_SLOPE(pg)	ISL7998X_REG((pg), 0x8a)
#define ISL7998X_REG_PX_ACA_MEAN_OFF_DNGAIN(pg)	ISL7998X_REG((pg), 0x8b)
#define ISL7998X_REG_PX_ACA_DELTA_CO_THRES(pg)	ISL7998X_REG((pg), 0x8c)
#define ISL7998X_REG_PX_ACA_DELTA_SLOPE(pg)	ISL7998X_REG((pg), 0x8d)
#define ISL7998X_REG_PX_ACA_LO_HI_AVG_THRES(pg)	ISL7998X_REG((pg), 0x8e)
#define ISL7998X_REG_PX_ACA_LO_MAX_LVL_CTL(pg)	ISL7998X_REG((pg), 0x8f)
#define ISL7998X_REG_PX_ACA_HI_MAX_LVL_CTL(pg)	ISL7998X_REG((pg), 0x90)
#define ISL7998X_REG_PX_ACA_LO_UPGAIN_CTL(pg)	ISL7998X_REG((pg), 0x91)
#define ISL7998X_REG_PX_ACA_LO_DNGAIN_CTL(pg)	ISL7998X_REG((pg), 0x92)
#define ISL7998X_REG_PX_ACA_HI_UPGAIN_CTL(pg)	ISL7998X_REG((pg), 0x93)
#define ISL7998X_REG_PX_ACA_HI_DNGAIN_CTL(pg)	ISL7998X_REG((pg), 0x94)
#define ISL7998X_REG_PX_ACA_LOPASS_FLT_COEF(pg)	ISL7998X_REG((pg), 0x95)
#define ISL7998X_REG_PX_ACA_PDF_INDEX(pg)	ISL7998X_REG((pg), 0x96)
#define ISL7998X_REG_PX_ACA_HIST_WIN_H_STT(pg)	ISL7998X_REG((pg), 0x97)
#define ISL7998X_REG_PX_ACA_HIST_WIN_H_SZ1(pg)	ISL7998X_REG((pg), 0x98)
#define ISL7998X_REG_PX_ACA_HIST_WIN_H_SZ2(pg)	ISL7998X_REG((pg), 0x99)
#define ISL7998X_REG_PX_ACA_HIST_WIN_V_STT(pg)	ISL7998X_REG((pg), 0x9a)
#define ISL7998X_REG_PX_ACA_HIST_WIN_V_SZ1(pg)	ISL7998X_REG((pg), 0x9b)
#define ISL7998X_REG_PX_ACA_HIST_WIN_V_SZ2(pg)	ISL7998X_REG((pg), 0x9c)
#define ISL7998X_REG_PX_ACA_Y_AVG(pg)		ISL7998X_REG((pg), 0xa0)
#define ISL7998X_REG_PX_ACA_Y_AVG_LIM(pg)	ISL7998X_REG((pg), 0xa1)
#define ISL7998X_REG_PX_ACA_LO_AVG(pg)		ISL7998X_REG((pg), 0xa2)
#define ISL7998X_REG_PX_ACA_HI_AVG(pg)		ISL7998X_REG((pg), 0xa3)
#define ISL7998X_REG_PX_ACA_Y_MAX(pg)		ISL7998X_REG((pg), 0xa4)
#define ISL7998X_REG_PX_ACA_Y_MIN(pg)		ISL7998X_REG((pg), 0xa5)
#define ISL7998X_REG_PX_ACA_MOFFSET(pg)		ISL7998X_REG((pg), 0xa6)
#define ISL7998X_REG_PX_ACA_LO_GAIN(pg)		ISL7998X_REG((pg), 0xa7)
#define ISL7998X_REG_PX_ACA_HI_GAIN(pg)		ISL7998X_REG((pg), 0xa8)
#define ISL7998X_REG_PX_ACA_LL_SLOPE(pg)	ISL7998X_REG((pg), 0xa9)
#define ISL7998X_REG_PX_ACA_LH_SLOPE(pg)	ISL7998X_REG((pg), 0xaa)
#define ISL7998X_REG_PX_ACA_HL_SLOPE(pg)	ISL7998X_REG((pg), 0xab)
#define ISL7998X_REG_PX_ACA_HH_SLOPE(pg)	ISL7998X_REG((pg), 0xac)
#define ISL7998X_REG_PX_ACA_X_LOW(pg)		ISL7998X_REG((pg), 0xad)
#define ISL7998X_REG_PX_ACA_X_MEAN(pg)		ISL7998X_REG((pg), 0xae)
#define ISL7998X_REG_PX_ACA_X_HIGH(pg)		ISL7998X_REG((pg), 0xaf)
#define ISL7998X_REG_PX_ACA_Y_LOW(pg)		ISL7998X_REG((pg), 0xb0)
#define ISL7998X_REG_PX_ACA_Y_MEAN(pg)		ISL7998X_REG((pg), 0xb1)
#define ISL7998X_REG_PX_ACA_Y_HIGH(pg)		ISL7998X_REG((pg), 0xb2)
#define ISL7998X_REG_PX_ACA_CTL_2(pg)		ISL7998X_REG((pg), 0xb3)
#define ISL7998X_REG_PX_ACA_CTL_3(pg)		ISL7998X_REG((pg), 0xb4)
#define ISL7998X_REG_PX_ACA_CTL_4(pg)		ISL7998X_REG((pg), 0xb5)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_HIST(pg)	ISL7998X_REG((pg), 0xc0)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_TL_H(pg)	ISL7998X_REG((pg), 0xc1)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_TL_L(pg)	ISL7998X_REG((pg), 0xc2)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_TL_H(pg)	ISL7998X_REG((pg), 0xc3)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_TL_L(pg)	ISL7998X_REG((pg), 0xc4)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_TR_H(pg)	ISL7998X_REG((pg), 0xc5)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_TR_L(pg)	ISL7998X_REG((pg), 0xc6)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_TR_H(pg)	ISL7998X_REG((pg), 0xc7)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_TR_L(pg)	ISL7998X_REG((pg), 0xc8)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_BL_H(pg)	ISL7998X_REG((pg), 0xc9)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_BL_L(pg)	ISL7998X_REG((pg), 0xca)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_BL_H(pg)	ISL7998X_REG((pg), 0xcb)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_BL_L(pg)	ISL7998X_REG((pg), 0xcc)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_BR_H(pg)	ISL7998X_REG((pg), 0xcd)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_BR_L(pg)	ISL7998X_REG((pg), 0xce)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_BR_H(pg)	ISL7998X_REG((pg), 0xcf)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_BR_L(pg)	ISL7998X_REG((pg), 0xd0)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_LM_H(pg)	ISL7998X_REG((pg), 0xd1)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_LM_L(pg)	ISL7998X_REG((pg), 0xd2)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_LM_H(pg)	ISL7998X_REG((pg), 0xd3)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_LM_L(pg)	ISL7998X_REG((pg), 0xd4)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_TM_H(pg)	ISL7998X_REG((pg), 0xd5)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_TM_L(pg)	ISL7998X_REG((pg), 0xd6)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_TM_H(pg)	ISL7998X_REG((pg), 0xd7)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_TM_L(pg)	ISL7998X_REG((pg), 0xd8)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_BM_H(pg)	ISL7998X_REG((pg), 0xd9)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_BM_L(pg)	ISL7998X_REG((pg), 0xda)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_BM_H(pg)	ISL7998X_REG((pg), 0xdb)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_BM_L(pg)	ISL7998X_REG((pg), 0xdc)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_RM_H(pg)	ISL7998X_REG((pg), 0xdd)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_X_RM_L(pg)	ISL7998X_REG((pg), 0xde)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_RM_H(pg)	ISL7998X_REG((pg), 0xdf)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_RM_L(pg)	ISL7998X_REG((pg), 0xe0)
#define ISL7998X_REG_PX_ACA_HIST_DATA_LO(pg)	ISL7998X_REG((pg), 0xe1)
#define ISL7998X_REG_PX_ACA_HIST_DATA_MID(pg)	ISL7998X_REG((pg), 0xe2)
#define ISL7998X_REG_PX_ACA_HIST_DATA_HI(pg)	ISL7998X_REG((pg), 0xe3)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_Y_CLR(pg)	ISL7998X_REG((pg), 0xe4)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_CB_CLR(pg)	ISL7998X_REG((pg), 0xe5)
#define ISL7998X_REG_PX_ACA_FLEX_WIN_CR_CLR(pg)	ISL7998X_REG((pg), 0xe6)
#define ISL7998X_REG_PX_ACA_XFER_HIST_HOST(pg)	ISL7998X_REG((pg), 0xe7)

#define ISL7998X_REG_P5_LI_ENGINE_CTL		ISL7998X_REG(5, 0x00)
#define ISL7998X_REG_P5_LI_ENGINE_LINE_CTL	ISL7998X_REG(5, 0x01)
#define ISL7998X_REG_P5_LI_ENGINE_PIC_WIDTH	ISL7998X_REG(5, 0x02)
#define ISL7998X_REG_P5_LI_ENGINE_SYNC_CTL	ISL7998X_REG(5, 0x03)
#define ISL7998X_REG_P5_LI_ENGINE_VC_ASSIGNMENT	ISL7998X_REG(5, 0x04)
#define ISL7998X_REG_P5_LI_ENGINE_TYPE_CTL	ISL7998X_REG(5, 0x05)
#define ISL7998X_REG_P5_LI_ENGINE_FIFO_CTL	ISL7998X_REG(5, 0x06)
#define ISL7998X_REG_P5_MIPI_READ_START_CTL	ISL7998X_REG(5, 0x07)
#define ISL7998X_REG_P5_PSEUDO_FRM_FIELD_CTL	ISL7998X_REG(5, 0x08)
#define ISL7998X_REG_P5_ONE_FIELD_MODE_CTL	ISL7998X_REG(5, 0x09)
#define ISL7998X_REG_P5_MIPI_INT_HW_TST_CTR	ISL7998X_REG(5, 0x0a)
#define ISL7998X_REG_P5_TP_GEN_BAR_PATTERN	ISL7998X_REG(5, 0x0b)
#define ISL7998X_REG_P5_MIPI_PCNT_PSFRM		ISL7998X_REG(5, 0x0c)
#define ISL7998X_REG_P5_LI_ENGINE_TP_GEN_CTL	ISL7998X_REG(5, 0x0d)
#define ISL7998X_REG_P5_MIPI_VBLANK_PSFRM	ISL7998X_REG(5, 0x0e)
#define ISL7998X_REG_P5_LI_ENGINE_CTL_2		ISL7998X_REG(5, 0x0f)
#define ISL7998X_REG_P5_MIPI_WCNT_1		ISL7998X_REG(5, 0x10)
#define ISL7998X_REG_P5_MIPI_WCNT_2		ISL7998X_REG(5, 0x11)
#define ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_1	ISL7998X_REG(5, 0x12)
#define ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_2	ISL7998X_REG(5, 0x13)
#define ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_3	ISL7998X_REG(5, 0x14)
#define ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_4	ISL7998X_REG(5, 0x15)
#define ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_5	ISL7998X_REG(5, 0x16)
#define ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_6	ISL7998X_REG(5, 0x17)
#define ISL7998X_REG_P5_MIPI_DPHY_PARAMS_1	ISL7998X_REG(5, 0x18)
#define ISL7998X_REG_P5_MIPI_DPHY_SOT_PERIOD	ISL7998X_REG(5, 0x19)
#define ISL7998X_REG_P5_MIPI_DPHY_EOT_PERIOD	ISL7998X_REG(5, 0x1a)
#define ISL7998X_REG_P5_MIPI_DPHY_PARAMS_2	ISL7998X_REG(5, 0x1b)
#define ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_7	ISL7998X_REG(5, 0x1c)
#define ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_8	ISL7998X_REG(5, 0x1d)
#define ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_9	ISL7998X_REG(5, 0x1e)
#define ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_10	ISL7998X_REG(5, 0x1f)
#define ISL7998X_REG_P5_TP_GEN_MIPI		ISL7998X_REG(5, 0x20)
#define ISL7998X_REG_P5_ESC_MODE_TIME_CTL	ISL7998X_REG(5, 0x21)
#define ISL7998X_REG_P5_AUTO_TEST_ERR_DET	ISL7998X_REG(5, 0x22)
#define ISL7998X_REG_P5_MIPI_TIMING		ISL7998X_REG(5, 0x23)
#define ISL7998X_REG_P5_PIC_HEIGHT_HIGH		ISL7998X_REG(5, 0x24)
#define ISL7998X_REG_P5_PIC_HEIGHT_LOW		ISL7998X_REG(5, 0x25)
#define ISL7998X_REG_P5_MIPI_SP_HS_TRL_CTL	ISL7998X_REG(5, 0x26)
#define ISL7998X_REG_P5_FIFO_THRSH_CNT_1	ISL7998X_REG(5, 0x28)
#define ISL7998X_REG_P5_FIFO_THRSH_CNT_2	ISL7998X_REG(5, 0x29)
#define ISL7998X_REG_P5_TP_GEN_RND_SYNC_CTL_1	ISL7998X_REG(5, 0x2a)
#define ISL7998X_REG_P5_TP_GEN_RND_SYNC_CTL_2	ISL7998X_REG(5, 0x2b)
#define ISL7998X_REG_P5_PSF_FIELD_END_CTL_1	ISL7998X_REG(5, 0x2c)
#define ISL7998X_REG_P5_PSF_FIELD_END_CTL_2	ISL7998X_REG(5, 0x2d)
#define ISL7998X_REG_P5_PSF_FIELD_END_CTL_3	ISL7998X_REG(5, 0x2e)
#define ISL7998X_REG_P5_PSF_FIELD_END_CTL_4	ISL7998X_REG(5, 0x2f)
#define ISL7998X_REG_P5_MIPI_ANA_DATA_CTL_1	ISL7998X_REG(5, 0x30)
#define ISL7998X_REG_P5_MIPI_ANA_DATA_CTL_2	ISL7998X_REG(5, 0x31)
#define ISL7998X_REG_P5_MIPI_ANA_CLK_CTL	ISL7998X_REG(5, 0x32)
#define ISL7998X_REG_P5_PLL_ANA_STATUS		ISL7998X_REG(5, 0x33)
#define ISL7998X_REG_P5_PLL_ANA_MISC_CTL	ISL7998X_REG(5, 0x34)
#define ISL7998X_REG_P5_MIPI_ANA		ISL7998X_REG(5, 0x35)
#define ISL7998X_REG_P5_PLL_ANA			ISL7998X_REG(5, 0x36)
#define ISL7998X_REG_P5_TOTAL_PF_LINE_CNT_1	ISL7998X_REG(5, 0x38)
#define ISL7998X_REG_P5_TOTAL_PF_LINE_CNT_2	ISL7998X_REG(5, 0x39)
#define ISL7998X_REG_P5_H_LINE_CNT_1		ISL7998X_REG(5, 0x3a)
#define ISL7998X_REG_P5_H_LINE_CNT_2		ISL7998X_REG(5, 0x3b)
#define ISL7998X_REG_P5_HIST_LINE_CNT_1		ISL7998X_REG(5, 0x3c)
#define ISL7998X_REG_P5_HIST_LINE_CNT_2		ISL7998X_REG(5, 0x3d)

static const struct reg_sequence isl7998x_init_seq_1[] = {
	{ ISL7998X_REG_P0_SHORT_DIAG_IRQ_EN, 0xff },
	{ ISL7998X_REG_PX_DEC_SDT(0x1), 0x00 },
	{ ISL7998X_REG_PX_DEC_SHORT_DET_CTL_1(0x1), 0x03 },
	{ ISL7998X_REG_PX_DEC_SDT(0x2), 0x00 },
	{ ISL7998X_REG_PX_DEC_SHORT_DET_CTL_1(0x2), 0x03 },
	{ ISL7998X_REG_PX_DEC_SDT(0x3), 0x00 },
	{ ISL7998X_REG_PX_DEC_SHORT_DET_CTL_1(0x3), 0x03 },
	{ ISL7998X_REG_PX_DEC_SDT(0x4), 0x00 },
	{ ISL7998X_REG_PX_DEC_SHORT_DET_CTL_1(0x4), 0x03 },
	{ ISL7998X_REG_P5_LI_ENGINE_CTL, 0x00 },
	{ ISL7998X_REG_P0_SW_RESET_CTL, 0x1f, 10 },
	{ ISL7998X_REG_P0_IO_BUFFER_CTL, 0x00 },
	{ ISL7998X_REG_P0_MPP2_SYNC_CTL, 0xc9 },
	{ ISL7998X_REG_P0_IRQ_SYNC_CTL, 0xc9 },
	{ ISL7998X_REG_P0_CHAN_1_IRQ, 0x03 },
	{ ISL7998X_REG_P0_CHAN_2_IRQ, 0x00 },
	{ ISL7998X_REG_P0_CHAN_3_IRQ, 0x00 },
	{ ISL7998X_REG_P0_CHAN_4_IRQ, 0x00 },
	{ ISL7998X_REG_P5_LI_ENGINE_CTL, 0x02 },
	{ ISL7998X_REG_P5_LI_ENGINE_LINE_CTL, 0x85 },
	{ ISL7998X_REG_P5_LI_ENGINE_PIC_WIDTH, 0xa0 },
	{ ISL7998X_REG_P5_LI_ENGINE_SYNC_CTL, 0x18 },
	{ ISL7998X_REG_P5_LI_ENGINE_TYPE_CTL, 0x40 },
	{ ISL7998X_REG_P5_LI_ENGINE_FIFO_CTL, 0x40 },
	{ ISL7998X_REG_P5_MIPI_WCNT_1, 0x05 },
	{ ISL7998X_REG_P5_MIPI_WCNT_2, 0xa0 },
	{ ISL7998X_REG_P5_TP_GEN_MIPI, 0x00 },
	{ ISL7998X_REG_P5_ESC_MODE_TIME_CTL, 0x0c },
	{ ISL7998X_REG_P5_MIPI_SP_HS_TRL_CTL, 0x00 },
	{ ISL7998X_REG_P5_TP_GEN_RND_SYNC_CTL_1, 0x00 },
	{ ISL7998X_REG_P5_TP_GEN_RND_SYNC_CTL_2, 0x19 },
	{ ISL7998X_REG_P5_PSF_FIELD_END_CTL_1, 0x18 },
	{ ISL7998X_REG_P5_PSF_FIELD_END_CTL_2, 0xf1 },
	{ ISL7998X_REG_P5_PSF_FIELD_END_CTL_3, 0x00 },
	{ ISL7998X_REG_P5_PSF_FIELD_END_CTL_4, 0xf1 },
	{ ISL7998X_REG_P5_MIPI_ANA_DATA_CTL_1, 0x00 },
	{ ISL7998X_REG_P5_MIPI_ANA_DATA_CTL_2, 0x00 },
	{ ISL7998X_REG_P5_MIPI_ANA_CLK_CTL, 0x00 },
	{ ISL7998X_REG_P5_PLL_ANA_STATUS, 0xc0 },
	{ ISL7998X_REG_P5_PLL_ANA_MISC_CTL, 0x18 },
	{ ISL7998X_REG_P5_PLL_ANA, 0x00 },
	{ ISL7998X_REG_P0_SW_RESET_CTL, 0x10, 10 },
	/* Page 0xf means write to all of pages 1,2,3,4 */
	{ ISL7998X_REG_PX_DEC_VDELAY_LO(0xf), 0x14 },
	{ ISL7998X_REG_PX_DEC_MISC3(0xf), 0xe6 },
	{ ISL7998X_REG_PX_DEC_CLMD(0xf), 0x85 },
	{ ISL7998X_REG_PX_DEC_H_DELAY_II_LOW(0xf), 0x11 },
	{ ISL7998X_REG_PX_ACA_XFER_HIST_HOST(0xf), 0x00 },
	{ ISL7998X_REG_P0_CLK_CTL_1, 0x1f },
	{ ISL7998X_REG_P0_CLK_CTL_2, 0x43 },
	{ ISL7998X_REG_P0_CLK_CTL_3, 0x4f },
};

static const struct reg_sequence isl7998x_init_seq_2[] = {
	{ ISL7998X_REG_P5_LI_ENGINE_SYNC_CTL, 0x10 },
	{ ISL7998X_REG_P5_LI_ENGINE_VC_ASSIGNMENT, 0xe4 },
	{ ISL7998X_REG_P5_LI_ENGINE_TYPE_CTL, 0x00 },
	{ ISL7998X_REG_P5_LI_ENGINE_FIFO_CTL, 0x60 },
	{ ISL7998X_REG_P5_MIPI_READ_START_CTL, 0x2b },
	{ ISL7998X_REG_P5_PSEUDO_FRM_FIELD_CTL, 0x02 },
	{ ISL7998X_REG_P5_ONE_FIELD_MODE_CTL, 0x00 },
	{ ISL7998X_REG_P5_MIPI_INT_HW_TST_CTR, 0x62 },
	{ ISL7998X_REG_P5_TP_GEN_BAR_PATTERN, 0x02 },
	{ ISL7998X_REG_P5_MIPI_PCNT_PSFRM, 0x36 },
	{ ISL7998X_REG_P5_LI_ENGINE_TP_GEN_CTL, 0x00 },
	{ ISL7998X_REG_P5_MIPI_VBLANK_PSFRM, 0x6c },
	{ ISL7998X_REG_P5_LI_ENGINE_CTL_2, 0x00 },
	{ ISL7998X_REG_P5_MIPI_WCNT_1, 0x05 },
	{ ISL7998X_REG_P5_MIPI_WCNT_2, 0xa0 },
	{ ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_1, 0x77 },
	{ ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_2, 0x17 },
	{ ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_3, 0x08 },
	{ ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_4, 0x38 },
	{ ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_5, 0x14 },
	{ ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_6, 0xf6 },
	{ ISL7998X_REG_P5_MIPI_DPHY_PARAMS_1, 0x00 },
	{ ISL7998X_REG_P5_MIPI_DPHY_SOT_PERIOD, 0x17 },
	{ ISL7998X_REG_P5_MIPI_DPHY_EOT_PERIOD, 0x0a },
	{ ISL7998X_REG_P5_MIPI_DPHY_PARAMS_2, 0x71 },
	{ ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_7, 0x7a },
	{ ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_8, 0x0f },
	{ ISL7998X_REG_P5_MIPI_DPHY_TIMING_CTL_9, 0x8c },
	{ ISL7998X_REG_P5_MIPI_SP_HS_TRL_CTL, 0x08 },
	{ ISL7998X_REG_P5_FIFO_THRSH_CNT_1, 0x01 },
	{ ISL7998X_REG_P5_FIFO_THRSH_CNT_2, 0x0e },
	{ ISL7998X_REG_P5_TP_GEN_RND_SYNC_CTL_1, 0x00 },
	{ ISL7998X_REG_P5_TP_GEN_RND_SYNC_CTL_2, 0x00 },
	{ ISL7998X_REG_P5_TOTAL_PF_LINE_CNT_1, 0x03 },
	{ ISL7998X_REG_P5_TOTAL_PF_LINE_CNT_2, 0xc0 },
	{ ISL7998X_REG_P5_H_LINE_CNT_1, 0x06 },
	{ ISL7998X_REG_P5_H_LINE_CNT_2, 0xb3 },
	{ ISL7998X_REG_P5_HIST_LINE_CNT_1, 0x00 },
	{ ISL7998X_REG_P5_HIST_LINE_CNT_2, 0xf1 },
	{ ISL7998X_REG_P5_LI_ENGINE_FIFO_CTL, 0x00 },
	{ ISL7998X_REG_P5_MIPI_ANA, 0x00 },
	/*
	 * Wait a bit after reset so that the chip can capture a frame
	 * and update internal line counters.
	 */
	{ ISL7998X_REG_P0_SW_RESET_CTL, 0x00, 50 },
};

enum isl7998x_pads {
	ISL7998X_PAD_OUT,
	ISL7998X_PAD_VIN1,
	ISL7998X_PAD_VIN2,
	ISL7998X_PAD_VIN3,
	ISL7998X_PAD_VIN4,
	ISL7998X_NUM_PADS
};

struct isl7998x_datafmt {
	u32			code;
	enum v4l2_colorspace	colorspace;
};

static const struct isl7998x_datafmt isl7998x_colour_fmts[] = {
	{ MEDIA_BUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_SRGB },
};

/* Menu items for LINK_FREQ V4L2 control */
static const s64 link_freq_menu_items[] = {
	/* 1 channel, 1 lane or 2 channels, 2 lanes */
	108000000,
	/* 2 channels, 1 lane or 4 channels, 2 lanes */
	216000000,
	/* 4 channels, 1 lane */
	432000000,
};

/* Menu items for TEST_PATTERN V4L2 control */
static const char * const isl7998x_test_pattern_menu[] = {
	"Disabled",
	"Enabled",
};

static const char * const isl7998x_test_pattern_bars[] = {
	"bbbbwb", "bbbwwb", "bbwbwb", "bbwwwb",
};

static const char * const isl7998x_test_pattern_colors[] = {
	"Yellow", "Blue", "Green", "Pink",
};

struct isl7998x_mode {
	unsigned int width;
	unsigned int height;
	enum v4l2_field field;
};

static const struct isl7998x_mode supported_modes[] = {
	{
		.width = 720,
		.height = 576,
		.field = V4L2_FIELD_SEQ_TB,
	},
	{
		.width = 720,
		.height = 480,
		.field = V4L2_FIELD_SEQ_BT,
	},
};

static const struct isl7998x_video_std {
	const v4l2_std_id norm;
	unsigned int id;
	const struct isl7998x_mode *mode;
} isl7998x_std_res[] = {
	{ V4L2_STD_NTSC_443,
	  ISL7998X_REG_PX_DEC_SDT_STANDARD_NTSC_443,
	  &supported_modes[1] },
	{ V4L2_STD_PAL_M,
	  ISL7998X_REG_PX_DEC_SDT_STANDARD_PAL_M,
	  &supported_modes[1] },
	{ V4L2_STD_PAL_Nc,
	  ISL7998X_REG_PX_DEC_SDT_STANDARD_PAL_CN,
	  &supported_modes[0] },
	{ V4L2_STD_PAL_N,
	  ISL7998X_REG_PX_DEC_SDT_STANDARD_PAL,
	  &supported_modes[0] },
	{ V4L2_STD_PAL_60,
	  ISL7998X_REG_PX_DEC_SDT_STANDARD_PAL_60,
	  &supported_modes[1] },
	{ V4L2_STD_NTSC,
	  ISL7998X_REG_PX_DEC_SDT_STANDARD_NTSC_M,
	  &supported_modes[1] },
	{ V4L2_STD_PAL,
	  ISL7998X_REG_PX_DEC_SDT_STANDARD_PAL,
	  &supported_modes[0] },
	{ V4L2_STD_SECAM,
	  ISL7998X_REG_PX_DEC_SDT_STANDARD_SECAM,
	  &supported_modes[0] },
	{ V4L2_STD_UNKNOWN,
	  ISL7998X_REG_PX_DEC_SDT_STANDARD_UNKNOWN,
	  &supported_modes[1] },
};

struct isl7998x {
	struct v4l2_subdev		subdev;
	struct regmap			*regmap;
	struct gpio_desc		*pd_gpio;
	struct gpio_desc		*rstb_gpio;
	unsigned int			nr_mipi_lanes;
	u32				nr_inputs;

	const struct isl7998x_datafmt	*fmt;
	v4l2_std_id			norm;
	struct media_pad		pads[ISL7998X_NUM_PADS];

	int				enabled;

	/* protect fmt, norm, enabled */
	struct mutex			lock;

	struct v4l2_ctrl_handler	ctrl_handler;
	/* protect ctrl_handler */
	struct mutex			ctrl_mutex;

	/* V4L2 Controls */
	struct v4l2_ctrl		*link_freq;
	u8				test_pattern;
	u8				test_pattern_bars;
	u8				test_pattern_chans;
	u8				test_pattern_color;
};

static struct isl7998x *sd_to_isl7998x(struct v4l2_subdev *sd)
{
	return container_of(sd, struct isl7998x, subdev);
}

static struct isl7998x *i2c_to_isl7998x(const struct i2c_client *client)
{
	return sd_to_isl7998x(i2c_get_clientdata(client));
}

static unsigned int isl7998x_norm_to_val(v4l2_std_id norm)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(isl7998x_std_res); i++)
		if (isl7998x_std_res[i].norm & norm)
			break;
	if (i == ARRAY_SIZE(isl7998x_std_res))
		return ISL7998X_REG_PX_DEC_SDT_STANDARD_UNKNOWN;

	return isl7998x_std_res[i].id;
}

static const struct isl7998x_mode *isl7998x_norm_to_mode(v4l2_std_id norm)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(isl7998x_std_res); i++)
		if (isl7998x_std_res[i].norm & norm)
			break;
	/* Use NTSC default resolution during standard detection */
	if (i == ARRAY_SIZE(isl7998x_std_res))
		return &supported_modes[1];

	return isl7998x_std_res[i].mode;
}

static int isl7998x_get_nr_inputs(struct device_node *of_node)
{
	struct device_node *port;
	unsigned int inputs = 0;
	unsigned int i;

	if (of_graph_get_endpoint_count(of_node) > ISL7998X_NUM_PADS)
		return -EINVAL;

	/*
	 * The driver does not provide means to remap the input ports. It
	 * always configures input ports to start from VID1. Ensure that the
	 * device tree is correct.
	 */
	for (i = ISL7998X_PAD_VIN1; i <= ISL7998X_PAD_VIN4; i++) {
		port = of_graph_get_port_by_id(of_node, i);
		if (!port)
			continue;

		inputs |= BIT(i);
		of_node_put(port);
	}

	switch (inputs) {
	case BIT(ISL7998X_PAD_VIN1):
		return 1;
	case BIT(ISL7998X_PAD_VIN1) | BIT(ISL7998X_PAD_VIN2):
		return 2;
	case BIT(ISL7998X_PAD_VIN1) | BIT(ISL7998X_PAD_VIN2) |
	     BIT(ISL7998X_PAD_VIN3) | BIT(ISL7998X_PAD_VIN4):
		return 4;
	default:
		return -EINVAL;
	}
}

static int isl7998x_wait_power_on(struct isl7998x *isl7998x)
{
	struct device *dev = isl7998x->subdev.dev;
	u32 chip_id;
	int ret;
	int err;

	ret = read_poll_timeout(regmap_read, err, !err, 2000, 20000, false,
				isl7998x->regmap,
				ISL7998X_REG_P0_PRODUCT_ID_CODE, &chip_id);
	if (ret) {
		dev_err(dev, "timeout while waiting for ISL7998X\n");
		return ret;
	}

	dev_dbg(dev, "Found ISL799%x\n", chip_id);

	return ret;
}

static int isl7998x_set_standard(struct isl7998x *isl7998x, v4l2_std_id norm)
{
	const struct isl7998x_mode *mode = isl7998x_norm_to_mode(norm);
	unsigned int val = isl7998x_norm_to_val(norm);
	unsigned int width = mode->width;
	unsigned int i;
	int ret;

	for (i = 0; i < ISL7998X_INPUTS; i++) {
		ret = regmap_write_bits(isl7998x->regmap,
					ISL7998X_REG_PX_DEC_SDT(i + 1),
					ISL7998X_REG_PX_DEC_SDT_STANDARD,
					val);
		if (ret)
			return ret;
	}

	ret = regmap_write(isl7998x->regmap,
			   ISL7998X_REG_P5_LI_ENGINE_LINE_CTL,
			   0x20 | ((width >> 7) & 0x1f));
	if (ret)
		return ret;

	ret = regmap_write(isl7998x->regmap,
			   ISL7998X_REG_P5_LI_ENGINE_PIC_WIDTH,
			   (width << 1) & 0xff);
	if (ret)
		return ret;

	return 0;
}

static int isl7998x_init(struct isl7998x *isl7998x)
{
	const unsigned int lanes = isl7998x->nr_mipi_lanes;
	const u32 isl7998x_video_in_chan_map[] = { 0x00, 0x11, 0x02, 0x02 };
	const struct reg_sequence isl7998x_init_seq_custom[] = {
		{ ISL7998X_REG_P0_VIDEO_IN_CHAN_CTL,
		  isl7998x_video_in_chan_map[isl7998x->nr_inputs - 1] },
		{ ISL7998X_REG_P0_CLK_CTL_4,
		  (lanes == 1) ? 0x40 : 0x41 },
		{ ISL7998X_REG_P5_LI_ENGINE_CTL,
		  (lanes == 1) ? 0x01 : 0x02 },
	};
	struct device *dev = isl7998x->subdev.dev;
	struct regmap *regmap = isl7998x->regmap;
	int ret;

	dev_dbg(dev, "configuring %d lanes for %d inputs (norm %s)\n",
		isl7998x->nr_mipi_lanes, isl7998x->nr_inputs,
		v4l2_norm_to_name(isl7998x->norm));

	ret = regmap_register_patch(regmap, isl7998x_init_seq_1,
				    ARRAY_SIZE(isl7998x_init_seq_1));
	if (ret)
		return ret;

	mutex_lock(&isl7998x->lock);
	ret = isl7998x_set_standard(isl7998x, isl7998x->norm);
	mutex_unlock(&isl7998x->lock);
	if (ret)
		return ret;

	ret = regmap_register_patch(regmap, isl7998x_init_seq_custom,
				    ARRAY_SIZE(isl7998x_init_seq_custom));
	if (ret)
		return ret;

	return regmap_register_patch(regmap, isl7998x_init_seq_2,
				     ARRAY_SIZE(isl7998x_init_seq_2));
}

static int isl7998x_set_test_pattern(struct isl7998x *isl7998x)
{
	const struct reg_sequence isl7998x_init_seq_tpg_off[] = {
		{ ISL7998X_REG_P5_LI_ENGINE_TP_GEN_CTL, 0 },
		{ ISL7998X_REG_P5_LI_ENGINE_CTL_2, 0 }
	};
	const struct reg_sequence isl7998x_init_seq_tpg_on[] = {
		{ ISL7998X_REG_P5_TP_GEN_BAR_PATTERN,
		  isl7998x->test_pattern_bars << 6 },
		{ ISL7998X_REG_P5_LI_ENGINE_CTL_2,
		  isl7998x->norm & V4L2_STD_PAL ? BIT(2) : 0 },
		{ ISL7998X_REG_P5_LI_ENGINE_TP_GEN_CTL,
		  (isl7998x->test_pattern_chans << 4) |
		  (isl7998x->test_pattern_color << 2) }
	};
	struct device *dev = isl7998x->subdev.dev;
	struct regmap *regmap = isl7998x->regmap;
	int ret;

	if (pm_runtime_get_if_in_use(dev) <= 0)
		return 0;

	if (isl7998x->test_pattern != 0) {
		dev_dbg(dev, "enabling test pattern: channels 0x%x, %s, %s\n",
			isl7998x->test_pattern_chans,
			isl7998x_test_pattern_bars[isl7998x->test_pattern_bars],
			isl7998x_test_pattern_colors[isl7998x->test_pattern_color]);
		ret = regmap_register_patch(regmap, isl7998x_init_seq_tpg_on,
					    ARRAY_SIZE(isl7998x_init_seq_tpg_on));
	} else {
		ret = regmap_register_patch(regmap, isl7998x_init_seq_tpg_off,
					    ARRAY_SIZE(isl7998x_init_seq_tpg_off));
	}

	pm_runtime_put(dev);

	return ret;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int isl7998x_g_register(struct v4l2_subdev *sd,
			       struct v4l2_dbg_register *reg)
{
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);
	int ret;
	u32 val;

	ret = regmap_read(isl7998x->regmap, reg->reg, &val);
	if (ret)
		return ret;

	reg->size = 1;
	reg->val = val;

	return 0;
}

static int isl7998x_s_register(struct v4l2_subdev *sd,
			       const struct v4l2_dbg_register *reg)
{
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);

	return regmap_write(isl7998x->regmap, reg->reg, reg->val);
}
#endif

static int isl7998x_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);

	mutex_lock(&isl7998x->lock);
	*norm = isl7998x->norm;
	mutex_unlock(&isl7998x->lock);

	return 0;
}

static int isl7998x_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;
	int ret = 0;

	mutex_lock(&isl7998x->lock);
	if (isl7998x->enabled) {
		ret = -EBUSY;
		mutex_unlock(&isl7998x->lock);
		return ret;
	}
	isl7998x->norm = norm;
	mutex_unlock(&isl7998x->lock);

	if (pm_runtime_get_if_in_use(dev) <= 0)
		return ret;

	ret = isl7998x_set_standard(isl7998x, norm);

	pm_runtime_put(dev);

	return ret;
}

static int isl7998x_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;
	unsigned int std_id[ISL7998X_INPUTS];
	unsigned int i;
	int ret;
	u32 reg;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	dev_dbg(dev, "starting video standard detection\n");

	mutex_lock(&isl7998x->lock);
	if (isl7998x->enabled) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = isl7998x_set_standard(isl7998x, V4L2_STD_UNKNOWN);
	if (ret)
		goto out_unlock;

	for (i = 0; i < ISL7998X_INPUTS; i++) {
		ret = regmap_write(isl7998x->regmap,
				   ISL7998X_REG_PX_DEC_SDTR(i + 1),
				   ISL7998X_REG_PX_DEC_SDTR_ATSTART);
		if (ret)
			goto out_reset_std;
	}

	for (i = 0; i < ISL7998X_INPUTS; i++) {
		ret = regmap_read_poll_timeout(isl7998x->regmap,
					       ISL7998X_REG_PX_DEC_SDT(i + 1),
					       reg,
					       !(reg & ISL7998X_REG_PX_DEC_SDT_DET),
					       2000, 500 * USEC_PER_MSEC);
		if (ret)
			goto out_reset_std;
		std_id[i] = FIELD_GET(ISL7998X_REG_PX_DEC_SDT_NOW, reg);
	}

	/*
	 * According to Renesas FAE, all input cameras must have the
	 * same standard on this chip.
	 */
	for (i = 0; i < isl7998x->nr_inputs; i++) {
		dev_dbg(dev, "input %d: detected %s\n",
			i, v4l2_norm_to_name(isl7998x_std_res[std_id[i]].norm));
		if (std_id[0] != std_id[i])
			dev_warn(dev,
				 "incompatible standards: %s on input %d (expected %s)\n",
				 v4l2_norm_to_name(isl7998x_std_res[std_id[i]].norm), i,
				 v4l2_norm_to_name(isl7998x_std_res[std_id[0]].norm));
	}

	*std = isl7998x_std_res[std_id[0]].norm;

out_reset_std:
	isl7998x_set_standard(isl7998x, isl7998x->norm);
out_unlock:
	mutex_unlock(&isl7998x->lock);
	pm_runtime_put(dev);

	return ret;
}

static int isl7998x_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	*std = V4L2_STD_ALL;

	return 0;
}

static int isl7998x_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;
	unsigned int i;
	int ret = 0;
	u32 reg;

	if (!pm_runtime_active(dev)) {
		*status |= V4L2_IN_ST_NO_POWER;
		return 0;
	}

	for (i = 0; i < isl7998x->nr_inputs; i++) {
		ret = regmap_read(isl7998x->regmap,
				  ISL7998X_REG_PX_DEC_STATUS_1(i + 1), &reg);
		if (!ret) {
			if (reg & ISL7998X_REG_PX_DEC_STATUS_1_VDLOSS)
				*status |= V4L2_IN_ST_NO_SIGNAL;
			if (!(reg & ISL7998X_REG_PX_DEC_STATUS_1_HLOCK))
				*status |= V4L2_IN_ST_NO_H_LOCK;
			if (!(reg & ISL7998X_REG_PX_DEC_STATUS_1_VLOCK))
				*status |= V4L2_IN_ST_NO_V_LOCK;
		}
	}

	return ret;
}

static int isl7998x_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;
	int ret = 0;
	u32 reg;

	dev_dbg(dev, "stream %s\n", enable ? "ON" : "OFF");

	mutex_lock(&isl7998x->lock);
	if (isl7998x->enabled == enable)
		goto out;
	isl7998x->enabled = enable;

	if (enable) {
		ret = isl7998x_set_test_pattern(isl7998x);
		if (ret)
			goto out;
	}

	regmap_read(isl7998x->regmap,
		    ISL7998X_REG_P5_LI_ENGINE_CTL, &reg);
	if (enable)
		reg &= ~BIT(7);
	else
		reg |= BIT(7);
	ret = regmap_write(isl7998x->regmap,
			   ISL7998X_REG_P5_LI_ENGINE_CTL, reg);

out:
	mutex_unlock(&isl7998x->lock);

	return ret;
}

static int isl7998x_pre_streamon(struct v4l2_subdev *sd, u32 flags)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;

	return pm_runtime_resume_and_get(dev);
}

static int isl7998x_post_streamoff(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;

	pm_runtime_put(dev);

	return 0;
}

static int isl7998x_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(isl7998x_colour_fmts))
		return -EINVAL;

	code->code = isl7998x_colour_fmts[code->index].code;

	return 0;
}

static int isl7998x_enum_frame_size(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != isl7998x_colour_fmts[0].code)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int isl7998x_get_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *format)
{
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);
	struct v4l2_mbus_framefmt *mf = &format->format;
	const struct isl7998x_mode *mode;

	mutex_lock(&isl7998x->lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		format->format = *v4l2_subdev_get_try_format(sd, sd_state,
							     format->pad);
		goto out;
	}

	mode = isl7998x_norm_to_mode(isl7998x->norm);

	mf->width	= mode->width;
	mf->height	= mode->height;
	mf->code	= isl7998x->fmt->code;
	mf->field	= mode->field;
	mf->colorspace	= 0;

out:
	mutex_unlock(&isl7998x->lock);

	return 0;
}

static int isl7998x_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *format)
{
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);
	struct v4l2_mbus_framefmt *mf = &format->format;
	const struct isl7998x_mode *mode;

	mutex_lock(&isl7998x->lock);

	mode = isl7998x_norm_to_mode(isl7998x->norm);

	mf->width = mode->width;
	mf->height = mode->height;
	mf->code = isl7998x->fmt->code;
	mf->field = mode->field;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		*v4l2_subdev_get_try_format(sd, sd_state, format->pad) = format->format;

	mutex_unlock(&isl7998x->lock);

	return 0;
}

static int isl7998x_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isl7998x *isl7998x = container_of(ctrl->handler,
						 struct isl7998x, ctrl_handler);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN_BARS:
		mutex_lock(&isl7998x->lock);
		isl7998x->test_pattern_bars = ctrl->val & 0x3;
		ret = isl7998x_set_test_pattern(isl7998x);
		mutex_unlock(&isl7998x->lock);
		break;
	case V4L2_CID_TEST_PATTERN_CHANNELS:
		mutex_lock(&isl7998x->lock);
		isl7998x->test_pattern_chans = ctrl->val & 0xf;
		ret = isl7998x_set_test_pattern(isl7998x);
		mutex_unlock(&isl7998x->lock);
		break;
	case V4L2_CID_TEST_PATTERN_COLOR:
		mutex_lock(&isl7998x->lock);
		isl7998x->test_pattern_color = ctrl->val & 0x3;
		ret = isl7998x_set_test_pattern(isl7998x);
		mutex_unlock(&isl7998x->lock);
		break;
	case V4L2_CID_TEST_PATTERN:
		mutex_lock(&isl7998x->lock);
		isl7998x->test_pattern = ctrl->val;
		ret = isl7998x_set_test_pattern(isl7998x);
		mutex_unlock(&isl7998x->lock);
		break;
	}

	return ret;
}

static const struct v4l2_subdev_core_ops isl7998x_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= isl7998x_g_register,
	.s_register	= isl7998x_s_register,
#endif
};

static const struct v4l2_subdev_video_ops isl7998x_subdev_video_ops = {
	.g_std		= isl7998x_g_std,
	.s_std		= isl7998x_s_std,
	.querystd	= isl7998x_querystd,
	.g_tvnorms	= isl7998x_g_tvnorms,
	.g_input_status	= isl7998x_g_input_status,
	.s_stream	= isl7998x_s_stream,
	.pre_streamon	= isl7998x_pre_streamon,
	.post_streamoff	= isl7998x_post_streamoff,
};

static const struct v4l2_subdev_pad_ops isl7998x_subdev_pad_ops = {
	.enum_mbus_code		= isl7998x_enum_mbus_code,
	.enum_frame_size	= isl7998x_enum_frame_size,
	.get_fmt		= isl7998x_get_fmt,
	.set_fmt		= isl7998x_set_fmt,
};

static const struct v4l2_subdev_ops isl7998x_subdev_ops = {
	.core		= &isl7998x_subdev_core_ops,
	.video		= &isl7998x_subdev_video_ops,
	.pad		= &isl7998x_subdev_pad_ops,
};

static const struct media_entity_operations isl7998x_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_ctrl_ops isl7998x_ctrl_ops = {
	.s_ctrl			= isl7998x_set_ctrl,
};

static const struct v4l2_ctrl_config isl7998x_ctrls[] = {
	{
		.ops		= &isl7998x_ctrl_ops,
		.id		= V4L2_CID_TEST_PATTERN_BARS,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Test Pattern Bars",
		.max		= ARRAY_SIZE(isl7998x_test_pattern_bars) - 1,
		.def		= 0,
		.qmenu		= isl7998x_test_pattern_bars,
	}, {
		.ops		= &isl7998x_ctrl_ops,
		.id		= V4L2_CID_TEST_PATTERN_CHANNELS,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Test Pattern Channels",
		.min		= 0,
		.max		= 0xf,
		.step		= 1,
		.def		= 0xf,
		.flags		= 0,
	}, {
		.ops		= &isl7998x_ctrl_ops,
		.id		= V4L2_CID_TEST_PATTERN_COLOR,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Test Pattern Color",
		.max		= ARRAY_SIZE(isl7998x_test_pattern_colors) - 1,
		.def		= 0,
		.qmenu		= isl7998x_test_pattern_colors,
	},
};

#define ISL7998X_REG_DECODER_ACA_READABLE_RANGE(page)			\
	/* Decoder range */						\
	regmap_reg_range(ISL7998X_REG_PX_DEC_INPUT_FMT(page),		\
			 ISL7998X_REG_PX_DEC_HS_DELAY_CTL(page)),	\
	regmap_reg_range(ISL7998X_REG_PX_DEC_ANCTL(page),		\
			 ISL7998X_REG_PX_DEC_CSC_CTL(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_BRIGHT(page),		\
			 ISL7998X_REG_PX_DEC_HUE(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_VERT_PEAK(page),		\
			 ISL7998X_REG_PX_DEC_CORING(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_SDT(page),			\
			 ISL7998X_REG_PX_DEC_SDTR(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_CLMPG(page),		\
			 ISL7998X_REG_PX_DEC_DATA_CONV(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_INTERNAL_TEST(page),	\
			 ISL7998X_REG_PX_DEC_INTERNAL_TEST(page)),	\
	regmap_reg_range(ISL7998X_REG_PX_DEC_H_DELAY_CTL(page),		\
			 ISL7998X_REG_PX_DEC_H_DELAY_II_LOW(page)),	\
	/* ACA range */							\
	regmap_reg_range(ISL7998X_REG_PX_ACA_CTL_1(page),		\
			 ISL7998X_REG_PX_ACA_HIST_WIN_V_SZ2(page)),	\
	regmap_reg_range(ISL7998X_REG_PX_ACA_Y_AVG(page),		\
			 ISL7998X_REG_PX_ACA_CTL_4(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_ACA_FLEX_WIN_HIST(page),	\
			 ISL7998X_REG_PX_ACA_XFER_HIST_HOST(page)),	\
	regmap_reg_range(ISL7998X_REG_PX_DEC_PAGE(page),		\
			 ISL7998X_REG_PX_DEC_PAGE(page))

#define ISL7998X_REG_DECODER_ACA_WRITEABLE_RANGE(page)			\
	/* Decoder range */						\
	regmap_reg_range(ISL7998X_REG_PX_DEC_INPUT_FMT(page),		\
			 ISL7998X_REG_PX_DEC_INPUT_FMT(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_HS_DELAY_CTL(page),	\
			 ISL7998X_REG_PX_DEC_HS_DELAY_CTL(page)),	\
	regmap_reg_range(ISL7998X_REG_PX_DEC_ANCTL(page),		\
			 ISL7998X_REG_PX_DEC_CSC_CTL(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_BRIGHT(page),		\
			 ISL7998X_REG_PX_DEC_HUE(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_VERT_PEAK(page),		\
			 ISL7998X_REG_PX_DEC_CORING(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_SDT(page),			\
			 ISL7998X_REG_PX_DEC_SDTR(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_CLMPG(page),		\
			 ISL7998X_REG_PX_DEC_MISC3(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_CLMD(page),		\
			 ISL7998X_REG_PX_DEC_DATA_CONV(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_INTERNAL_TEST(page),	\
			 ISL7998X_REG_PX_DEC_INTERNAL_TEST(page)),	\
	regmap_reg_range(ISL7998X_REG_PX_DEC_H_DELAY_CTL(page),		\
			 ISL7998X_REG_PX_DEC_H_DELAY_II_LOW(page)),	\
	/* ACA range */							\
	regmap_reg_range(ISL7998X_REG_PX_ACA_CTL_1(page),		\
			 ISL7998X_REG_PX_ACA_HIST_WIN_V_SZ2(page)),	\
	regmap_reg_range(ISL7998X_REG_PX_ACA_CTL_2(page),		\
			 ISL7998X_REG_PX_ACA_CTL_4(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_ACA_FLEX_WIN_HIST(page),	\
			 ISL7998X_REG_PX_ACA_HIST_DATA_LO(page)),	\
	regmap_reg_range(ISL7998X_REG_PX_ACA_XFER_HIST_HOST(page),	\
			 ISL7998X_REG_PX_ACA_XFER_HIST_HOST(page)),	\
	regmap_reg_range(ISL7998X_REG_PX_DEC_PAGE(page),		\
			 ISL7998X_REG_PX_DEC_PAGE(page))

#define ISL7998X_REG_DECODER_ACA_VOLATILE_RANGE(page)			\
	/* Decoder range */						\
	regmap_reg_range(ISL7998X_REG_PX_DEC_STATUS_1(page),		\
			 ISL7998X_REG_PX_DEC_STATUS_1(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_SDT(page),			\
			 ISL7998X_REG_PX_DEC_SDT(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_DEC_MVSN(page),		\
			 ISL7998X_REG_PX_DEC_HFREF(page)),		\
	/* ACA range */							\
	regmap_reg_range(ISL7998X_REG_PX_ACA_Y_AVG(page),		\
			 ISL7998X_REG_PX_ACA_Y_HIGH(page)),		\
	regmap_reg_range(ISL7998X_REG_PX_ACA_HIST_DATA_LO(page),	\
			 ISL7998X_REG_PX_ACA_FLEX_WIN_CR_CLR(page))

static const struct regmap_range isl7998x_readable_ranges[] = {
	regmap_reg_range(ISL7998X_REG_P0_PRODUCT_ID_CODE,
			 ISL7998X_REG_P0_IRQ_SYNC_CTL),
	regmap_reg_range(ISL7998X_REG_P0_INTERRUPT_STATUS,
			 ISL7998X_REG_P0_CLOCK_DELAY),
	regmap_reg_range(ISL7998X_REG_PX_DEC_PAGE(0),
			 ISL7998X_REG_PX_DEC_PAGE(0)),

	ISL7998X_REG_DECODER_ACA_READABLE_RANGE(1),
	ISL7998X_REG_DECODER_ACA_READABLE_RANGE(2),
	ISL7998X_REG_DECODER_ACA_READABLE_RANGE(3),
	ISL7998X_REG_DECODER_ACA_READABLE_RANGE(4),

	regmap_reg_range(ISL7998X_REG_P5_LI_ENGINE_CTL,
			 ISL7998X_REG_P5_MIPI_SP_HS_TRL_CTL),
	regmap_reg_range(ISL7998X_REG_P5_FIFO_THRSH_CNT_1,
			 ISL7998X_REG_P5_PLL_ANA),
	regmap_reg_range(ISL7998X_REG_P5_TOTAL_PF_LINE_CNT_1,
			 ISL7998X_REG_P5_HIST_LINE_CNT_2),
	regmap_reg_range(ISL7998X_REG_PX_DEC_PAGE(5),
			 ISL7998X_REG_PX_DEC_PAGE(5)),
};

static const struct regmap_range isl7998x_writeable_ranges[] = {
	regmap_reg_range(ISL7998X_REG_P0_SW_RESET_CTL,
			 ISL7998X_REG_P0_IRQ_SYNC_CTL),
	regmap_reg_range(ISL7998X_REG_P0_CHAN_1_IRQ,
			 ISL7998X_REG_P0_SHORT_DIAG_IRQ_EN),
	regmap_reg_range(ISL7998X_REG_P0_CLOCK_DELAY,
			 ISL7998X_REG_P0_CLOCK_DELAY),
	regmap_reg_range(ISL7998X_REG_PX_DEC_PAGE(0),
			 ISL7998X_REG_PX_DEC_PAGE(0)),

	ISL7998X_REG_DECODER_ACA_WRITEABLE_RANGE(1),
	ISL7998X_REG_DECODER_ACA_WRITEABLE_RANGE(2),
	ISL7998X_REG_DECODER_ACA_WRITEABLE_RANGE(3),
	ISL7998X_REG_DECODER_ACA_WRITEABLE_RANGE(4),

	regmap_reg_range(ISL7998X_REG_P5_LI_ENGINE_CTL,
			 ISL7998X_REG_P5_ESC_MODE_TIME_CTL),
	regmap_reg_range(ISL7998X_REG_P5_MIPI_SP_HS_TRL_CTL,
			 ISL7998X_REG_P5_PLL_ANA),
	regmap_reg_range(ISL7998X_REG_P5_TOTAL_PF_LINE_CNT_1,
			 ISL7998X_REG_P5_HIST_LINE_CNT_2),
	regmap_reg_range(ISL7998X_REG_PX_DEC_PAGE(5),
			 ISL7998X_REG_PX_DEC_PAGE(5)),

	ISL7998X_REG_DECODER_ACA_WRITEABLE_RANGE(0xf),
};

static const struct regmap_range isl7998x_volatile_ranges[] = {
	/* Product id code register is used to check availability */
	regmap_reg_range(ISL7998X_REG_P0_PRODUCT_ID_CODE,
			 ISL7998X_REG_P0_PRODUCT_ID_CODE),
	regmap_reg_range(ISL7998X_REG_P0_MPP1_SYNC_CTL,
			 ISL7998X_REG_P0_IRQ_SYNC_CTL),
	regmap_reg_range(ISL7998X_REG_P0_INTERRUPT_STATUS,
			 ISL7998X_REG_P0_INTERRUPT_STATUS),
	regmap_reg_range(ISL7998X_REG_P0_CHAN_1_STATUS,
			 ISL7998X_REG_P0_SHORT_DIAG_STATUS),

	ISL7998X_REG_DECODER_ACA_VOLATILE_RANGE(1),
	ISL7998X_REG_DECODER_ACA_VOLATILE_RANGE(2),
	ISL7998X_REG_DECODER_ACA_VOLATILE_RANGE(3),
	ISL7998X_REG_DECODER_ACA_VOLATILE_RANGE(4),

	regmap_reg_range(ISL7998X_REG_P5_AUTO_TEST_ERR_DET,
			 ISL7998X_REG_P5_PIC_HEIGHT_LOW),
};

static const struct regmap_access_table isl7998x_readable_table = {
	.yes_ranges = isl7998x_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(isl7998x_readable_ranges),
};

static const struct regmap_access_table isl7998x_writeable_table = {
	.yes_ranges = isl7998x_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(isl7998x_writeable_ranges),
};

static const struct regmap_access_table isl7998x_volatile_table = {
	.yes_ranges = isl7998x_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(isl7998x_volatile_ranges),
};

static const struct regmap_range_cfg isl7998x_ranges[] = {
	{
		.range_min	= ISL7998X_REG_PN_BASE(0),
		.range_max	= ISL7998X_REG_PX_ACA_XFER_HIST_HOST(0xf),
		.selector_reg	= ISL7998X_REG_PX_DEC_PAGE(0),
		.selector_mask	= ISL7998X_REG_PX_DEC_PAGE_MASK,
		.window_start	= 0,
		.window_len	= 256,
	}
};

static const struct regmap_config isl7998x_regmap = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= ISL7998X_REG_PX_ACA_XFER_HIST_HOST(0xf),
	.ranges		= isl7998x_ranges,
	.num_ranges	= ARRAY_SIZE(isl7998x_ranges),
	.rd_table	= &isl7998x_readable_table,
	.wr_table	= &isl7998x_writeable_table,
	.volatile_table	= &isl7998x_volatile_table,
	.cache_type	= REGCACHE_RBTREE,
};

static int isl7998x_mc_init(struct isl7998x *isl7998x)
{
	unsigned int i;

	isl7998x->subdev.entity.ops = &isl7998x_entity_ops;
	isl7998x->subdev.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;

	isl7998x->pads[ISL7998X_PAD_OUT].flags = MEDIA_PAD_FL_SOURCE;
	for (i = ISL7998X_PAD_VIN1; i < ISL7998X_NUM_PADS; i++)
		isl7998x->pads[i].flags = MEDIA_PAD_FL_SINK;

	return media_entity_pads_init(&isl7998x->subdev.entity,
				      ISL7998X_NUM_PADS,
				      isl7998x->pads);
}

static int get_link_freq_menu_index(unsigned int lanes,
				    unsigned int inputs)
{
	int ret = -EINVAL;

	switch (lanes) {
	case 1:
		if (inputs == 1)
			ret = 0;
		if (inputs == 2)
			ret = 1;
		if (inputs == 4)
			ret = 2;
		break;
	case 2:
		if (inputs == 2)
			ret = 0;
		if (inputs == 4)
			ret = 1;
		break;
	default:
		break;
	}

	return ret;
}

static void isl7998x_remove_controls(struct isl7998x *isl7998x)
{
	v4l2_ctrl_handler_free(&isl7998x->ctrl_handler);
	mutex_destroy(&isl7998x->ctrl_mutex);
}

static int isl7998x_init_controls(struct isl7998x *isl7998x)
{
	struct v4l2_subdev *sd = &isl7998x->subdev;
	int link_freq_index;
	unsigned int i;
	int ret;

	ret = v4l2_ctrl_handler_init(&isl7998x->ctrl_handler,
				     2 + ARRAY_SIZE(isl7998x_ctrls));
	if (ret)
		return ret;

	mutex_init(&isl7998x->ctrl_mutex);
	isl7998x->ctrl_handler.lock = &isl7998x->ctrl_mutex;
	link_freq_index = get_link_freq_menu_index(isl7998x->nr_mipi_lanes,
						   isl7998x->nr_inputs);
	if (link_freq_index < 0 ||
	    link_freq_index >= ARRAY_SIZE(link_freq_menu_items)) {
		dev_err(sd->dev,
			"failed to find MIPI link freq: %d lanes, %d inputs\n",
			isl7998x->nr_mipi_lanes, isl7998x->nr_inputs);
		ret = -EINVAL;
		goto err;
	}

	isl7998x->link_freq = v4l2_ctrl_new_int_menu(&isl7998x->ctrl_handler,
						     &isl7998x_ctrl_ops,
						     V4L2_CID_LINK_FREQ,
						     ARRAY_SIZE(link_freq_menu_items) - 1,
						     link_freq_index,
						     link_freq_menu_items);
	if (isl7998x->link_freq)
		isl7998x->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	for (i = 0; i < ARRAY_SIZE(isl7998x_ctrls); i++)
		v4l2_ctrl_new_custom(&isl7998x->ctrl_handler,
				     &isl7998x_ctrls[i], NULL);

	v4l2_ctrl_new_std_menu_items(&isl7998x->ctrl_handler,
				     &isl7998x_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(isl7998x_test_pattern_menu) - 1,
				     0, 0, isl7998x_test_pattern_menu);

	ret = isl7998x->ctrl_handler.error;
	if (ret)
		goto err;

	isl7998x->subdev.ctrl_handler = &isl7998x->ctrl_handler;
	v4l2_ctrl_handler_setup(&isl7998x->ctrl_handler);

	return 0;

err:
	isl7998x_remove_controls(isl7998x);

	return ret;
}

static int isl7998x_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct v4l2_fwnode_endpoint endpoint = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep;
	struct isl7998x *isl7998x;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	int nr_inputs;
	int ret;

	ret = i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA);
	if (!ret) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	isl7998x = devm_kzalloc(dev, sizeof(*isl7998x), GFP_KERNEL);
	if (!isl7998x)
		return -ENOMEM;

	isl7998x->pd_gpio = devm_gpiod_get_optional(dev, "powerdown",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(isl7998x->pd_gpio))
		return dev_err_probe(dev, PTR_ERR(isl7998x->pd_gpio),
				     "Failed to retrieve/request PD GPIO\n");

	isl7998x->rstb_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(isl7998x->rstb_gpio))
		return dev_err_probe(dev, PTR_ERR(isl7998x->rstb_gpio),
				     "Failed to retrieve/request RSTB GPIO\n");

	isl7998x->regmap = devm_regmap_init_i2c(client, &isl7998x_regmap);
	if (IS_ERR(isl7998x->regmap))
		return dev_err_probe(dev, PTR_ERR(isl7998x->regmap),
				     "Failed to allocate register map\n");

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
					     ISL7998X_PAD_OUT, 0, 0);
	if (!ep)
		return dev_err_probe(dev, -EINVAL, "Missing endpoint node\n");

	ret = v4l2_fwnode_endpoint_parse(ep, &endpoint);
	fwnode_handle_put(ep);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to parse endpoint\n");

	if (endpoint.bus.mipi_csi2.num_data_lanes == 0 ||
	    endpoint.bus.mipi_csi2.num_data_lanes > 2)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid number of MIPI lanes\n");

	isl7998x->nr_mipi_lanes = endpoint.bus.mipi_csi2.num_data_lanes;

	nr_inputs = isl7998x_get_nr_inputs(dev->of_node);
	if (nr_inputs < 0)
		return dev_err_probe(dev, nr_inputs,
				     "Invalid number of input ports\n");
	isl7998x->nr_inputs = nr_inputs;

	v4l2_i2c_subdev_init(&isl7998x->subdev, client, &isl7998x_subdev_ops);
	isl7998x->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	ret = isl7998x_mc_init(isl7998x);
	if (ret < 0)
		return ret;

	isl7998x->fmt = &isl7998x_colour_fmts[0];
	isl7998x->norm = V4L2_STD_NTSC;
	isl7998x->enabled = 0;

	mutex_init(&isl7998x->lock);

	ret = isl7998x_init_controls(isl7998x);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_async_register_subdev(&isl7998x->subdev);
	if (ret < 0)
		goto err_controls_cleanup;

	pm_runtime_enable(dev);

	return 0;

err_controls_cleanup:
	isl7998x_remove_controls(isl7998x);
err_entity_cleanup:
	media_entity_cleanup(&isl7998x->subdev.entity);

	return ret;
}

static void isl7998x_remove(struct i2c_client *client)
{
	struct isl7998x *isl7998x = i2c_to_isl7998x(client);

	pm_runtime_disable(&client->dev);
	v4l2_async_unregister_subdev(&isl7998x->subdev);
	isl7998x_remove_controls(isl7998x);
	media_entity_cleanup(&isl7998x->subdev.entity);
}

static const struct of_device_id isl7998x_of_match[] = {
	{ .compatible = "isil,isl79987", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, isl7998x_of_match);

static const struct i2c_device_id isl7998x_id[] = {
	{ "isl79987", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, isl7998x_id);

static int __maybe_unused isl7998x_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);
	int ret;

	gpiod_set_value(isl7998x->rstb_gpio, 1);
	gpiod_set_value(isl7998x->pd_gpio, 0);
	gpiod_set_value(isl7998x->rstb_gpio, 0);

	ret = isl7998x_wait_power_on(isl7998x);
	if (ret)
		goto err;

	ret = isl7998x_init(isl7998x);
	if (ret)
		goto err;

	return 0;

err:
	gpiod_set_value(isl7998x->pd_gpio, 1);

	return ret;
}

static int __maybe_unused isl7998x_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct isl7998x *isl7998x = sd_to_isl7998x(sd);

	gpiod_set_value(isl7998x->pd_gpio, 1);

	return 0;
}

static const struct dev_pm_ops isl7998x_pm_ops = {
	SET_RUNTIME_PM_OPS(isl7998x_runtime_suspend,
			   isl7998x_runtime_resume,
			   NULL)
};

static struct i2c_driver isl7998x_i2c_driver = {
	.driver = {
		.name = "isl7998x",
		.of_match_table = of_match_ptr(isl7998x_of_match),
		.pm = &isl7998x_pm_ops,
	},
	.probe_new	= isl7998x_probe,
	.remove		= isl7998x_remove,
	.id_table	= isl7998x_id,
};

module_i2c_driver(isl7998x_i2c_driver);

MODULE_DESCRIPTION("Intersil ISL7998x Analog to MIPI CSI-2/BT656 decoder");
MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_LICENSE("GPL v2");

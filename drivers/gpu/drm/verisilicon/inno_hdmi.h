/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 *    Zheng Yang <zhengyang@rock-chips.com>
 *    Yakir Yang <ykk@rock-chips.com>
 */

#ifndef __INNO_HDMI_H__
#define __INNO_HDMI_H__

#define DDC_SEGMENT_ADDR		0x30

enum PWR_MODE {
	NORMAL,
	LOWER_PWR,
};

#define HDMI_SCL_RATE			(100*1000)
#define DDC_BUS_FREQ_L			0x4b
#define DDC_BUS_FREQ_H			0x4c

#define HDMI_SYS_CTRL			0x00
#define m_RST_ANALOG			(1 << 6)
#define v_RST_ANALOG			(0 << 6)
#define v_NOT_RST_ANALOG		(1 << 6)
#define m_RST_DIGITAL			(1 << 5)
#define v_RST_DIGITAL			(0 << 5)
#define v_NOT_RST_DIGITAL		(1 << 5)
#define m_REG_CLK_INV			(1 << 4)
#define v_REG_CLK_NOT_INV		(0 << 4)
#define v_REG_CLK_INV			(1 << 4)
#define m_VCLK_INV			(1 << 3)
#define v_VCLK_NOT_INV			(0 << 3)
#define v_VCLK_INV			(1 << 3)
#define m_REG_CLK_SOURCE		(1 << 2)
#define v_REG_CLK_SOURCE_TMDS		(0 << 2)
#define v_REG_CLK_SOURCE_SYS		(1 << 2)
#define m_POWER				(1 << 1)
#define v_PWR_ON			(0 << 1)
#define v_PWR_OFF			(1 << 1)
#define m_INT_POL			(1 << 0)
#define v_INT_POL_HIGH			1
#define v_INT_POL_LOW			0

#define HDMI_VIDEO_CONTRL1		0x01
#define m_VIDEO_INPUT_FORMAT		(7 << 1)
#define m_DE_SOURCE			(1 << 0)
#define v_VIDEO_INPUT_FORMAT(n)		(n << 1)
#define v_DE_EXTERNAL			1
#define v_DE_INTERNAL			0
enum {
	VIDEO_INPUT_SDR_RGB444 = 0,
	VIDEO_INPUT_DDR_RGB444 = 5,
	VIDEO_INPUT_DDR_YCBCR422 = 6
};

#define HDMI_VIDEO_CONTRL2		0x02
#define m_VIDEO_OUTPUT_COLOR		(3 << 6)
#define m_VIDEO_INPUT_BITS		(3 << 4)
#define m_VIDEO_INPUT_CSP		(1 << 0)
#define v_VIDEO_OUTPUT_COLOR(n)		(((n) & 0x3) << 6)
#define v_VIDEO_INPUT_BITS(n)		(n << 4)
#define v_VIDEO_INPUT_CSP(n)		(n << 0)
enum {
	VIDEO_INPUT_12BITS = 0,
	VIDEO_INPUT_10BITS = 1,
	VIDEO_INPUT_REVERT = 2,
	VIDEO_INPUT_8BITS = 3,
};

#define HDMI_VIDEO_CONTRL		0x03
#define m_VIDEO_AUTO_CSC		(1 << 7)
#define v_VIDEO_AUTO_CSC(n)		(n << 7)
#define m_VIDEO_C0_C2_SWAP		(1 << 0)
#define v_VIDEO_C0_C2_SWAP(n)		(n << 0)
enum {
	C0_C2_CHANGE_ENABLE = 0,
	C0_C2_CHANGE_DISABLE = 1,
	AUTO_CSC_DISABLE = 0,
	AUTO_CSC_ENABLE = 1,
};

#define HDMI_VIDEO_CONTRL3		0x04
#define m_COLOR_DEPTH_NOT_INDICATED	(1 << 4)
#define m_SOF				(1 << 3)
#define m_COLOR_RANGE			(1 << 2)
#define m_CSC				(1 << 0)
#define v_COLOR_DEPTH_NOT_INDICATED(n)	((n) << 4)
#define v_SOF_ENABLE			(0 << 3)
#define v_SOF_DISABLE			(1 << 3)
#define v_COLOR_RANGE_FULL		(1 << 2)
#define v_COLOR_RANGE_LIMITED		(0 << 2)
#define v_CSC_ENABLE			1
#define v_CSC_DISABLE			0

#define HDMI_AV_MUTE			0x05
#define m_AVMUTE_CLEAR			(1 << 7)
#define m_AVMUTE_ENABLE			(1 << 6)
#define m_AUDIO_MUTE			(1 << 1)
#define m_VIDEO_BLACK			(1 << 0)
#define v_AVMUTE_CLEAR(n)		(n << 7)
#define v_AVMUTE_ENABLE(n)		(n << 6)
#define v_AUDIO_MUTE(n)			(n << 1)
#define v_VIDEO_MUTE(n)			(n << 0)

#define HDMI_VIDEO_TIMING_CTL		0x08
#define v_HSYNC_POLARITY(n)		(n << 3)
#define v_VSYNC_POLARITY(n)		(n << 2)
#define v_INETLACE(n)			(n << 1)
#define v_EXTERANL_VIDEO(n)		(n << 0)

#define HDMI_VIDEO_EXT_HTOTAL_L		0x09
#define HDMI_VIDEO_EXT_HTOTAL_H		0x0a
#define HDMI_VIDEO_EXT_HBLANK_L		0x0b
#define HDMI_VIDEO_EXT_HBLANK_H		0x0c
#define HDMI_VIDEO_EXT_HDELAY_L		0x0d
#define HDMI_VIDEO_EXT_HDELAY_H		0x0e
#define HDMI_VIDEO_EXT_HDURATION_L	0x0f
#define HDMI_VIDEO_EXT_HDURATION_H	0x10
#define HDMI_VIDEO_EXT_VTOTAL_L		0x11
#define HDMI_VIDEO_EXT_VTOTAL_H		0x12
#define HDMI_VIDEO_EXT_VBLANK		0x13
#define HDMI_VIDEO_EXT_VDELAY		0x14
#define HDMI_VIDEO_EXT_VDURATION	0x15

#define HDMI_VIDEO_CSC_COEF		0x18

#define HDMI_AUDIO_CTRL1		0x35
enum {
	CTS_SOURCE_INTERNAL = 0,
	CTS_SOURCE_EXTERNAL = 1,
};
#define v_CTS_SOURCE(n)			(n << 7)

enum {
	DOWNSAMPLE_DISABLE = 0,
	DOWNSAMPLE_1_2 = 1,
	DOWNSAMPLE_1_4 = 2,
};
#define v_DOWN_SAMPLE(n)		(n << 5)

enum {
	AUDIO_SOURCE_IIS = 0,
	AUDIO_SOURCE_SPDIF = 1,
};
#define v_AUDIO_SOURCE(n)		(n << 3)

#define v_MCLK_ENABLE(n)		(n << 2)
enum {
	MCLK_128FS = 0,
	MCLK_256FS = 1,
	MCLK_384FS = 2,
	MCLK_512FS = 3,
};
#define v_MCLK_RATIO(n)			(n)

#define AUDIO_SAMPLE_RATE		0x37
enum {
	AUDIO_32K = 0x3,
	AUDIO_441K = 0x0,
	AUDIO_48K = 0x2,
	AUDIO_882K = 0x8,
	AUDIO_96K = 0xa,
	AUDIO_1764K = 0xc,
	AUDIO_192K = 0xe,
};

#define AUDIO_I2S_MODE			0x38
enum {
	I2S_CHANNEL_1_2 = 1,
	I2S_CHANNEL_3_4 = 3,
	I2S_CHANNEL_5_6 = 7,
	I2S_CHANNEL_7_8 = 0xf
};
#define v_I2S_CHANNEL(n)		((n) << 2)
enum {
	I2S_STANDARD = 0,
	I2S_LEFT_JUSTIFIED = 1,
	I2S_RIGHT_JUSTIFIED = 2,
};
#define v_I2S_MODE(n)			(n)

#define AUDIO_I2S_MAP			0x39
#define AUDIO_I2S_SWAPS_SPDIF		0x3a
#define v_SPIDF_FREQ(n)			(n)

#define N_32K				0x1000
#define N_441K				0x1880
#define N_882K				0x3100
#define N_1764K				0x6200
#define N_48K				0x1800
#define N_96K				0x3000
#define N_192K				0x6000

#define HDMI_AUDIO_CHANNEL_STATUS	0x3e
#define m_AUDIO_STATUS_NLPCM		(1 << 7)
#define m_AUDIO_STATUS_USE		(1 << 6)
#define m_AUDIO_STATUS_COPYRIGHT	(1 << 5)
#define m_AUDIO_STATUS_ADDITION		(3 << 2)
#define m_AUDIO_STATUS_CLK_ACCURACY	(2 << 0)
#define v_AUDIO_STATUS_NLPCM(n)		((n & 1) << 7)
#define AUDIO_N_H			0x3f
#define AUDIO_N_M			0x40
#define AUDIO_N_L			0x41

#define HDMI_AUDIO_CTS_H		0x45
#define HDMI_AUDIO_CTS_M		0x46
#define HDMI_AUDIO_CTS_L		0x47

#define HDMI_DDC_CLK_L			0x4b
#define HDMI_DDC_CLK_H			0x4c

#define HDMI_EDID_SEGMENT_POINTER	0x4d
#define HDMI_EDID_WORD_ADDR		0x4e
#define HDMI_EDID_FIFO_OFFSET		0x4f
#define HDMI_EDID_FIFO_ADDR		0x50

#define HDMI_PACKET_SEND_MANUAL		0x9c
#define HDMI_PACKET_SEND_AUTO		0x9d
#define m_PACKET_GCP_EN			(1 << 7)
#define m_PACKET_MSI_EN			(1 << 6)
#define m_PACKET_SDI_EN			(1 << 5)
#define m_PACKET_VSI_EN			(1 << 4)
#define v_PACKET_GCP_EN(n)		((n & 1) << 7)
#define v_PACKET_MSI_EN(n)		((n & 1) << 6)
#define v_PACKET_SDI_EN(n)		((n & 1) << 5)
#define v_PACKET_VSI_EN(n)		((n & 1) << 4)

#define HDMI_CONTROL_PACKET_BUF_INDEX	0x9f
enum {
	INFOFRAME_VSI = 0x05,
	INFOFRAME_AVI = 0x06,
	INFOFRAME_AAI = 0x08,
};

#define HDMI_CONTROL_PACKET_ADDR	0xa0
#define HDMI_MAXIMUM_INFO_FRAME_SIZE	0x11
enum {
	AVI_COLOR_MODE_RGB = 0,
	AVI_COLOR_MODE_YCBCR422 = 1,
	AVI_COLOR_MODE_YCBCR444 = 2,
	AVI_COLORIMETRY_NO_DATA = 0,

	AVI_COLORIMETRY_SMPTE_170M = 1,
	AVI_COLORIMETRY_ITU709 = 2,
	AVI_COLORIMETRY_EXTENDED = 3,

	AVI_CODED_FRAME_ASPECT_NO_DATA = 0,
	AVI_CODED_FRAME_ASPECT_4_3 = 1,
	AVI_CODED_FRAME_ASPECT_16_9 = 2,

	ACTIVE_ASPECT_RATE_SAME_AS_CODED_FRAME = 0x08,
	ACTIVE_ASPECT_RATE_4_3 = 0x09,
	ACTIVE_ASPECT_RATE_16_9 = 0x0A,
	ACTIVE_ASPECT_RATE_14_9 = 0x0B,
};

#define HDMI_HDCP_CTRL			0x52
#define m_HDMI_DVI			(1 << 1)
#define v_HDMI_DVI(n)			(n << 1)

#define HDMI_INTERRUPT_MASK1		0xc0
#define HDMI_INTERRUPT_STATUS1		0xc1
#define	m_INT_ACTIVE_VSYNC		(1 << 5)
#define m_INT_EDID_READY		(1 << 2)

#define HDMI_INTERRUPT_MASK2		0xc2
#define HDMI_INTERRUPT_STATUS2		0xc3
#define m_INT_HDCP_ERR			(1 << 7)
#define m_INT_BKSV_FLAG			(1 << 6)
#define m_INT_HDCP_OK			(1 << 4)

#define HDMI_STATUS			0xc8
#define m_HOTPLUG			(1 << 7)
#define m_MASK_INT_HOTPLUG		(1 << 5)
#define m_INT_HOTPLUG			(1 << 1)
#define v_MASK_INT_HOTPLUG(n)		((n & 0x1) << 5)

#define HDMI_COLORBAR                   0xc9

#define HDMI_PHY_SYNC			0xce
#define HDMI_PHY_SYS_CTL		0xe0
#define m_TMDS_CLK_SOURCE		(1 << 5)
#define v_TMDS_FROM_PLL			(0 << 5)
#define v_TMDS_FROM_GEN			(1 << 5)
#define m_PHASE_CLK			(1 << 4)
#define v_DEFAULT_PHASE			(0 << 4)
#define v_SYNC_PHASE			(1 << 4)
#define m_TMDS_CURRENT_PWR		(1 << 3)
#define v_TURN_ON_CURRENT		(0 << 3)
#define v_CAT_OFF_CURRENT		(1 << 3)
#define m_BANDGAP_PWR			(1 << 2)
#define v_BANDGAP_PWR_UP		(0 << 2)
#define v_BANDGAP_PWR_DOWN		(1 << 2)
#define m_PLL_PWR			(1 << 1)
#define v_PLL_PWR_UP			(0 << 1)
#define v_PLL_PWR_DOWN			(1 << 1)
#define m_TMDS_CHG_PWR			(1 << 0)
#define v_TMDS_CHG_PWR_UP		(0 << 0)
#define v_TMDS_CHG_PWR_DOWN		(1 << 0)

#define HDMI_PHY_CHG_PWR		0xe1
#define v_CLK_CHG_PWR(n)		((n & 1) << 3)
#define v_DATA_CHG_PWR(n)		((n & 7) << 0)

#define HDMI_PHY_DRIVER			0xe2
#define v_CLK_MAIN_DRIVER(n)		(n << 4)
#define v_DATA_MAIN_DRIVER(n)		(n << 0)

#define HDMI_PHY_PRE_EMPHASIS		0xe3
#define v_PRE_EMPHASIS(n)		((n & 7) << 4)
#define v_CLK_PRE_DRIVER(n)		((n & 3) << 2)
#define v_DATA_PRE_DRIVER(n)		((n & 3) << 0)

#define HDMI_PHY_FEEDBACK_DIV_RATIO_LOW		0xe7
#define v_FEEDBACK_DIV_LOW(n)			(n & 0xff)
#define HDMI_PHY_FEEDBACK_DIV_RATIO_HIGH	0xe8
#define v_FEEDBACK_DIV_HIGH(n)			(n & 1)

#define HDMI_PHY_PRE_DIV_RATIO		0xed
#define v_PRE_DIV_RATIO(n)		(n & 0x1f)

#define HDMI_CEC_CTRL			0xd0
#define m_ADJUST_FOR_HISENSE		(1 << 6)
#define m_REJECT_RX_BROADCAST		(1 << 5)
#define m_BUSFREETIME_ENABLE		(1 << 2)
#define m_REJECT_RX			(1 << 1)
#define m_START_TX			(1 << 0)

#define HDMI_CEC_DATA			0xd1
#define HDMI_CEC_TX_OFFSET		0xd2
#define HDMI_CEC_RX_OFFSET		0xd3
#define HDMI_CEC_CLK_H			0xd4
#define HDMI_CEC_CLK_L			0xd5
#define HDMI_CEC_TX_LENGTH		0xd6
#define HDMI_CEC_RX_LENGTH		0xd7
#define HDMI_CEC_TX_INT_MASK		0xd8
#define m_TX_DONE			(1 << 3)
#define m_TX_NOACK			(1 << 2)
#define m_TX_BROADCAST_REJ		(1 << 1)
#define m_TX_BUSNOTFREE			(1 << 0)

#define HDMI_CEC_RX_INT_MASK		0xd9
#define m_RX_LA_ERR			(1 << 4)
#define m_RX_GLITCH			(1 << 3)
#define m_RX_DONE			(1 << 0)

#define HDMI_CEC_TX_INT			0xda
#define HDMI_CEC_RX_INT			0xdb
#define HDMI_CEC_BUSFREETIME_L		0xdc
#define HDMI_CEC_BUSFREETIME_H		0xdd
#define HDMI_CEC_LOGICADDR		0xde


#define HDMI_ESD_STATUS			0x1ce

#define HDMI_REG_1A0			0x1a0
#define m_PLL_CTRL				(1 << 4)
#define m_VCO_CTRL				(1 << 3)
#define m_OUTPUT_CLK			(1 << 2)
#define m_PIX_DIV				(1 << 1)
#define m_PRE_PLL_POWER			(1 << 0)

typedef enum {
	VIC_1440x480i60 = 6,
	VIC_640x480p60 = 1,
	VIC_720x480p60 = 2,
	VIC_1280x720p60 = 4,
	VIC_1920x1080p60 = 16,
	VIC_4096x2160p30 = 95,
	VIC_4096x2160p60 = 97,
} vic_code_t;

































#define UPDATE(x, h, l)		(((x) << (l)) & GENMASK((h), (l)))

	/* REG: 0x1a0 */
#define INNO_PCLK_VCO_DIV_5_MASK			BIT(1)
#define INNO_PCLK_VCO_DIV_5(x)				UPDATE(x, 1, 1)
#define INNO_PRE_PLL_POWER_DOWN				BIT(0)

	/* REG: 0x1a1 */
#define INNO_PRE_PLL_PRE_DIV_MASK			GENMASK(5, 0)
#define INNO_PRE_PLL_PRE_DIV(x)				UPDATE(x, 5, 0)


	/* REG: 0xa2 */
	/* unset means center spread */
#define INNO_SPREAD_SPECTRUM_MOD_DOWN			BIT(7)
#define INNO_SPREAD_SPECTRUM_MOD_DISABLE		BIT(6)
#define INNO_PRE_PLL_FRAC_DIV_DISABLE			UPDATE(3, 5, 4)
#define INNO_PRE_PLL_FB_DIV_11_8_MASK			GENMASK(3, 0)
#define INNO_PRE_PLL_FB_DIV_11_8(x)				UPDATE((x) >> 8, 3, 0)

	/* REG: 0xa3 */
#define INNO_PRE_PLL_FB_DIV_7_0(x)				UPDATE(x, 7, 0)

	/* REG: 0xa4*/
#define INNO_PRE_PLL_TMDSCLK_DIV_C_MASK			GENMASK(1, 0)
#define INNO_PRE_PLL_TMDSCLK_DIV_C(x)			UPDATE(x, 1, 0)
#define INNO_PRE_PLL_TMDSCLK_DIV_B_MASK			GENMASK(3, 2)
#define INNO_PRE_PLL_TMDSCLK_DIV_B(x)			UPDATE(x, 3, 2)
#define INNO_PRE_PLL_TMDSCLK_DIV_A_MASK			GENMASK(5, 4)
#define INNO_PRE_PLL_TMDSCLK_DIV_A(x)			UPDATE(x, 5, 4)
	/* REG: 0xa5 */
#define INNO_PRE_PLL_PCLK_DIV_B_SHIFT			5
#define INNO_PRE_PLL_PCLK_DIV_B_MASK			GENMASK(6, 5)
#define INNO_PRE_PLL_PCLK_DIV_B(x)				UPDATE(x, 6, 5)
#define INNO_PRE_PLL_PCLK_DIV_A_MASK			GENMASK(4, 0)
#define INNO_PRE_PLL_PCLK_DIV_A(x)				UPDATE(x, 4, 0)

	/* REG: 0xa6 */
#define INNO_PRE_PLL_PCLK_DIV_C_SHIFT			5
#define INNO_PRE_PLL_PCLK_DIV_C_MASK			GENMASK(6, 5)
#define INNO_PRE_PLL_PCLK_DIV_C(x)				UPDATE(x, 6, 5)
#define INNO_PRE_PLL_PCLK_DIV_D_MASK			GENMASK(4, 0)
#define INNO_PRE_PLL_PCLK_DIV_D(x)				UPDATE(x, 4, 0)

			/* REG: 0xd1 */
#define INNO_PRE_PLL_FRAC_DIV_23_16(x)			UPDATE((x) >> 16, 7, 0)
			/* REG: 0xd2 */
#define INNO_PRE_PLL_FRAC_DIV_15_8(x)			UPDATE((x) >> 8, 7, 0)
			/* REG: 0xd3 */
#define INNO_PRE_PLL_FRAC_DIV_7_0(x)			UPDATE(x, 7, 0)

	/* REG: 0x1aa */
#define INNO_POST_PLL_POST_DIV_ENABLE			GENMASK(3, 2)
#define INNO_POST_PLL_REFCLK_SEL_TMDS			BIT(1)
#define INNO_POST_PLL_POWER_DOWN				BIT(0)
#define INNO_POST_PLL_FB_DIV_8(x)			UPDATE(((x) >> 8) <<4 , 4, 4)

	/* REG:0x1ab */
#define INNO_POST_PLL_Pre_DIV_MASK			GENMASK(5, 0)
#define INNO_POST_PLL_PRE_DIV(x)			UPDATE(x, 5, 0)
	/* REG: 0x1ac */
#define INNO_POST_PLL_FB_DIV_7_0(x)			UPDATE(x, 7, 0)
	/* REG: 0x1ad */
#define INNO_POST_PLL_POST_DIV_MASK			GENMASK(2, 0)
#define INNO_POST_PLL_POST_DIV_2			0x0
#define INNO_POST_PLL_POST_DIV_4			0x1
#define INNO_POST_PLL_POST_DIV_8			0x3
	/* REG: 0x1af */
#define INNO_POST_PLL_LOCK_STATUS			BIT(0)
	/* REG: 0x1b0 */
#define INNO_BANDGAP_ENABLE				BIT(2)
	/* REG: 0x1b2 */
#define INNO_TMDS_CLK_DRIVER_EN			BIT(3)
#define INNO_TMDS_D2_DRIVER_EN			BIT(2)
#define INNO_TMDS_D1_DRIVER_EN			BIT(1)
#define INNO_TMDS_D0_DRIVER_EN			BIT(0)
#define INNO_TMDS_DRIVER_ENABLE		(INNO_TMDS_CLK_DRIVER_EN | \
							INNO_TMDS_D2_DRIVER_EN | \
							INNO_TMDS_D1_DRIVER_EN | \
							INNO_TMDS_D0_DRIVER_EN)
	/* REG:0x1c5 */
#define INNO_BYPASS_TERM_RESISTOR_CALIB		BIT(7)
#define INNO_TERM_RESISTOR_CALIB_SPEED_14_8(x)	UPDATE((x) >> 8, 6, 0)
	/* REG:0x1c6 */
#define INNO_TERM_RESISTOR_CALIB_SPEED_7_0(x)		UPDATE(x, 7, 0)
	/* REG:0x1c7 */
#define INNO_TERM_RESISTOR_100				UPDATE(0, 2, 1)
#define INNO_TERM_RESISTOR_125				UPDATE(1, 2, 1)
#define INNO_TERM_RESISTOR_150				UPDATE(2, 2, 1)
#define INNO_TERM_RESISTOR_200				UPDATE(3, 2, 1)
	/* REG 0x1c8 - 0x1cb */
#define INNO_ESD_DETECT_MASK				GENMASK(5, 0)
#define INNO_ESD_DETECT_340MV				(0x0 << 6)
#define INNO_ESD_DETECT_280MV				(0x1 << 6)
#define INNO_ESD_DETECT_260MV				(0x2 << 6)
#define INNO_ESD_DETECT_240MV				(0x3 << 6)
	/* resistors can be used in parallel */
#define INNO_TMDS_TERM_RESIST_MASK			GENMASK(5, 0)
#define INNO_TMDS_TERM_RESIST_125			BIT(5)
#define INNO_TMDS_TERM_RESIST_250			BIT(4)
#define INNO_TMDS_TERM_RESIST_500			BIT(3)
#define INNO_TMDS_TERM_RESIST_1000			BIT(2)
#define INNO_TMDS_TERM_RESIST_2000			BIT(1)
#define INNO_TMDS_TERM_RESIST_4000			BIT(0)

struct pre_pll_config {
	unsigned long pixclock;
	unsigned long tmdsclock;
	u8 prediv;
	u16 fbdiv;
	u8 tmds_div_a;
	u8 tmds_div_b;
	u8 tmds_div_c;
	u8 pclk_div_a;
	u8 pclk_div_b;
	u8 pclk_div_c;
	u8 pclk_div_d;
	u8 vco_div_5_en;
	u32 fracdiv;
};

struct post_pll_config {
	unsigned long tmdsclock;
	u8 prediv;
	u16 fbdiv;
	u8 postdiv;
	u8 post_div_en;
	u8 version;
};

struct phy_config {
	unsigned long	tmdsclock;
	u8		regs[14];
};

typedef struct register_value {
	u16 reg;
	u8 value;
} reg_value_t;

#endif /* __INNO_HDMI_H__ */

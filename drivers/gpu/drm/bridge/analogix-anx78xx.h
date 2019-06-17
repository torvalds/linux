/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2016, Analogix Semiconductor. All rights reserved.
 */

#ifndef __ANX78xx_H
#define __ANX78xx_H

#define TX_P0				0x70
#define TX_P1				0x7a
#define TX_P2				0x72

#define RX_P0				0x7e
#define RX_P1				0x80

/***************************************************************/
/* Register definition of device address 0x7e                  */
/***************************************************************/

/*
 * System Control and Status
 */

/* Software Reset Register 1 */
#define SP_SOFTWARE_RESET1_REG		0x11
#define SP_VIDEO_RST			BIT(4)
#define SP_HDCP_MAN_RST			BIT(2)
#define SP_TMDS_RST			BIT(1)
#define SP_SW_MAN_RST			BIT(0)

/* System Status Register */
#define SP_SYSTEM_STATUS_REG		0x14
#define SP_TMDS_CLOCK_DET		BIT(1)
#define SP_TMDS_DE_DET			BIT(0)

/* HDMI Status Register */
#define SP_HDMI_STATUS_REG		0x15
#define SP_HDMI_AUD_LAYOUT		BIT(3)
#define SP_HDMI_DET			BIT(0)
#  define SP_DVI_MODE			0
#  define SP_HDMI_MODE			1

/* HDMI Mute Control Register */
#define SP_HDMI_MUTE_CTRL_REG		0x16
#define SP_AUD_MUTE			BIT(1)
#define SP_VID_MUTE			BIT(0)

/* System Power Down Register 1 */
#define SP_SYSTEM_POWER_DOWN1_REG	0x18
#define SP_PWDN_CTRL			BIT(0)

/*
 * Audio and Video Auto Control
 */

/* Auto Audio and Video Control register */
#define SP_AUDVID_CTRL_REG		0x20
#define SP_AVC_OE			BIT(7)
#define SP_AAC_OE			BIT(6)
#define SP_AVC_EN			BIT(1)
#define SP_AAC_EN			BIT(0)

/* Audio Exception Enable Registers */
#define SP_AUD_EXCEPTION_ENABLE_BASE	(0x24 - 1)
/* Bits for Audio Exception Enable Register 3 */
#define SP_AEC_EN21			BIT(5)

/*
 * Interrupt
 */

/* Interrupt Status Register 1 */
#define SP_INT_STATUS1_REG		0x31
/* Bits for Interrupt Status Register 1 */
#define SP_HDMI_DVI			BIT(7)
#define SP_CKDT_CHG			BIT(6)
#define SP_SCDT_CHG			BIT(5)
#define SP_PCLK_CHG			BIT(4)
#define SP_PLL_UNLOCK			BIT(3)
#define SP_CABLE_PLUG_CHG		BIT(2)
#define SP_SET_MUTE			BIT(1)
#define SP_SW_INTR			BIT(0)
/* Bits for Interrupt Status Register 2 */
#define SP_HDCP_ERR			BIT(5)
#define SP_AUDIO_SAMPLE_CHG		BIT(0)	/* undocumented */
/* Bits for Interrupt Status Register 3 */
#define SP_AUD_MODE_CHG			BIT(0)
/* Bits for Interrupt Status Register 5 */
#define SP_AUDIO_RCV			BIT(0)
/* Bits for Interrupt Status Register 6 */
#define SP_INT_STATUS6_REG		0x36
#define SP_CTS_RCV			BIT(7)
#define SP_NEW_AUD_PKT			BIT(4)
#define SP_NEW_AVI_PKT			BIT(1)
#define SP_NEW_CP_PKT			BIT(0)
/* Bits for Interrupt Status Register 7 */
#define SP_NO_VSI			BIT(7)
#define SP_NEW_VS			BIT(4)

/* Interrupt Mask 1 Status Registers */
#define SP_INT_MASK1_REG		0x41

/* HDMI US TIMER Control Register */
#define SP_HDMI_US_TIMER_CTRL_REG	0x49
#define SP_MS_TIMER_MARGIN_10_8_MASK	0x07

/*
 * TMDS Control
 */

/* TMDS Control Registers */
#define SP_TMDS_CTRL_BASE		(0x50 - 1)
/* Bits for TMDS Control Register 7 */
#define SP_PD_RT			BIT(0)

/*
 * Video Control
 */

/* Video Status Register */
#define SP_VIDEO_STATUS_REG		0x70
#define SP_COLOR_DEPTH_MASK		0xf0
#define SP_COLOR_DEPTH_SHIFT		4
#  define SP_COLOR_DEPTH_MODE_LEGACY	0x00
#  define SP_COLOR_DEPTH_MODE_24BIT	0x04
#  define SP_COLOR_DEPTH_MODE_30BIT	0x05
#  define SP_COLOR_DEPTH_MODE_36BIT	0x06
#  define SP_COLOR_DEPTH_MODE_48BIT	0x07

/* Video Data Range Control Register */
#define SP_VID_DATA_RANGE_CTRL_REG	0x83
#define SP_R2Y_INPUT_LIMIT		BIT(1)

/* Pixel Clock High Resolution Counter Registers */
#define SP_PCLK_HIGHRES_CNT_BASE	(0x8c - 1)

/*
 * Audio Control
 */

/* Number of Audio Channels Status Registers */
#define SP_AUD_CH_STATUS_REG_NUM	6

/* Audio IN S/PDIF Channel Status Registers */
#define SP_AUD_SPDIF_CH_STATUS_BASE	0xc7

/* Audio IN S/PDIF Channel Status Register 4 */
#define SP_FS_FREQ_MASK			0x0f
#  define SP_FS_FREQ_44100HZ		0x00
#  define SP_FS_FREQ_48000HZ		0x02
#  define SP_FS_FREQ_32000HZ		0x03
#  define SP_FS_FREQ_88200HZ		0x08
#  define SP_FS_FREQ_96000HZ		0x0a
#  define SP_FS_FREQ_176400HZ		0x0c
#  define SP_FS_FREQ_192000HZ		0x0e

/*
 * Micellaneous Control Block
 */

/* CHIP Control Register */
#define SP_CHIP_CTRL_REG		0xe3
#define SP_MAN_HDMI5V_DET		BIT(3)
#define SP_PLLLOCK_CKDT_EN		BIT(2)
#define SP_ANALOG_CKDT_EN		BIT(1)
#define SP_DIGITAL_CKDT_EN		BIT(0)

/* Packet Receiving Status Register */
#define SP_PACKET_RECEIVING_STATUS_REG	0xf3
#define SP_AVI_RCVD			BIT(5)
#define SP_VSI_RCVD			BIT(1)

/***************************************************************/
/* Register definition of device address 0x80                  */
/***************************************************************/

/* HDCP BCAPS Shadow Register */
#define SP_HDCP_BCAPS_SHADOW_REG	0x2a
#define SP_BCAPS_REPEATER		BIT(5)

/* HDCP Status Register */
#define SP_RX_HDCP_STATUS_REG		0x3f
#define SP_AUTH_EN			BIT(4)

/*
 * InfoFrame and Control Packet Registers
 */

/* AVI InfoFrame packet checksum */
#define SP_AVI_INFOFRAME_CHECKSUM	0xa3

/* AVI InfoFrame Registers */
#define SP_AVI_INFOFRAME_DATA_BASE	0xa4

#define SP_AVI_COLOR_F_MASK		0x60
#define SP_AVI_COLOR_F_SHIFT		5

/* Audio InfoFrame Registers */
#define SP_AUD_INFOFRAME_DATA_BASE	0xc4
#define SP_AUD_INFOFRAME_LAYOUT_MASK	0x0f

/* MPEG/HDMI Vendor Specific InfoFrame Packet type code */
#define SP_MPEG_VS_INFOFRAME_TYPE_REG	0xe0

/* MPEG/HDMI Vendor Specific InfoFrame Packet length */
#define SP_MPEG_VS_INFOFRAME_LEN_REG	0xe2

/* MPEG/HDMI Vendor Specific InfoFrame Packet version number */
#define SP_MPEG_VS_INFOFRAME_VER_REG	0xe1

/* MPEG/HDMI Vendor Specific InfoFrame Packet content */
#define SP_MPEG_VS_INFOFRAME_DATA_BASE	0xe4

/* General Control Packet Register */
#define SP_GENERAL_CTRL_PACKET_REG	0x9f
#define SP_CLEAR_AVMUTE			BIT(4)
#define SP_SET_AVMUTE			BIT(0)

/***************************************************************/
/* Register definition of device address 0x70                  */
/***************************************************************/

/* HDCP Status Register */
#define SP_TX_HDCP_STATUS_REG		0x00
#define SP_AUTH_FAIL			BIT(5)
#define SP_AUTHEN_PASS			BIT(1)

/* HDCP Control Register 0 */
#define SP_HDCP_CTRL0_REG		0x01
#define SP_RX_REPEATER			BIT(6)
#define SP_RE_AUTH			BIT(5)
#define SP_SW_AUTH_OK			BIT(4)
#define SP_HARD_AUTH_EN			BIT(3)
#define SP_HDCP_ENC_EN			BIT(2)
#define SP_BKSV_SRM_PASS		BIT(1)
#define SP_KSVLIST_VLD			BIT(0)
/* HDCP Function Enabled */
#define SP_HDCP_FUNCTION_ENABLED	(BIT(0) | BIT(1) | BIT(2) | BIT(3))

/* HDCP Receiver BSTATUS Register 0 */
#define	SP_HDCP_RX_BSTATUS0_REG		0x1b
/* HDCP Receiver BSTATUS Register 1 */
#define	SP_HDCP_RX_BSTATUS1_REG		0x1c

/* HDCP Embedded "Blue Screen" Content Registers */
#define SP_HDCP_VID0_BLUE_SCREEN_REG	0x2c
#define SP_HDCP_VID1_BLUE_SCREEN_REG	0x2d
#define SP_HDCP_VID2_BLUE_SCREEN_REG	0x2e

/* HDCP Wait R0 Timing Register */
#define SP_HDCP_WAIT_R0_TIME_REG	0x40

/* HDCP Link Integrity Check Timer Register */
#define SP_HDCP_LINK_CHECK_TIMER_REG	0x41

/* HDCP Repeater Ready Wait Timer Register */
#define SP_HDCP_RPTR_RDY_WAIT_TIME_REG	0x42

/* HDCP Auto Timer Register */
#define SP_HDCP_AUTO_TIMER_REG		0x51

/* HDCP Key Status Register */
#define SP_HDCP_KEY_STATUS_REG		0x5e

/* HDCP Key Command Register */
#define SP_HDCP_KEY_COMMAND_REG		0x5f
#define SP_DISABLE_SYNC_HDCP		BIT(2)

/* OTP Memory Key Protection Registers */
#define SP_OTP_KEY_PROTECT1_REG		0x60
#define SP_OTP_KEY_PROTECT2_REG		0x61
#define SP_OTP_KEY_PROTECT3_REG		0x62
#define SP_OTP_PSW1			0xa2
#define SP_OTP_PSW2			0x7e
#define SP_OTP_PSW3			0xc6

/* DP System Control Registers */
#define SP_DP_SYSTEM_CTRL_BASE		(0x80 - 1)
/* Bits for DP System Control Register 2 */
#define SP_CHA_STA			BIT(2)
/* Bits for DP System Control Register 3 */
#define SP_HPD_STATUS			BIT(6)
#define SP_STRM_VALID			BIT(2)
/* Bits for DP System Control Register 4 */
#define SP_ENHANCED_MODE		BIT(3)

/* DP Video Control Register */
#define SP_DP_VIDEO_CTRL_REG		0x84
#define SP_COLOR_F_MASK			0x06
#define SP_COLOR_F_SHIFT		1
#define SP_BPC_MASK			0xe0
#define SP_BPC_SHIFT			5
#  define SP_BPC_6BITS			0x00
#  define SP_BPC_8BITS			0x01
#  define SP_BPC_10BITS			0x02
#  define SP_BPC_12BITS			0x03

/* DP Audio Control Register */
#define SP_DP_AUDIO_CTRL_REG		0x87
#define SP_AUD_EN			BIT(0)

/* 10us Pulse Generate Timer Registers */
#define SP_I2C_GEN_10US_TIMER0_REG	0x88
#define SP_I2C_GEN_10US_TIMER1_REG	0x89

/* Packet Send Control Register */
#define SP_PACKET_SEND_CTRL_REG		0x90
#define SP_AUD_IF_UP			BIT(7)
#define SP_AVI_IF_UD			BIT(6)
#define SP_MPEG_IF_UD			BIT(5)
#define SP_SPD_IF_UD			BIT(4)
#define SP_AUD_IF_EN			BIT(3)
#define SP_AVI_IF_EN			BIT(2)
#define SP_MPEG_IF_EN			BIT(1)
#define SP_SPD_IF_EN			BIT(0)

/* DP HDCP Control Register */
#define SP_DP_HDCP_CTRL_REG		0x92
#define SP_AUTO_EN			BIT(7)
#define SP_AUTO_START			BIT(5)
#define SP_LINK_POLLING			BIT(1)

/* DP Main Link Bandwidth Setting Register */
#define SP_DP_MAIN_LINK_BW_SET_REG	0xa0
#define SP_LINK_BW_SET_MASK		0x1f
#define SP_INITIAL_SLIM_M_AUD_SEL	BIT(5)

/* DP Training Pattern Set Register */
#define SP_DP_TRAINING_PATTERN_SET_REG	0xa2

/* DP Lane 0 Link Training Control Register */
#define SP_DP_LANE0_LT_CTRL_REG		0xa3
#define SP_TX_SW_SET_MASK		0x1b
#define SP_MAX_PRE_REACH		BIT(5)
#define SP_MAX_DRIVE_REACH		BIT(4)
#define SP_PRE_EMP_LEVEL1		BIT(3)
#define SP_DRVIE_CURRENT_LEVEL1		BIT(0)

/* DP Link Training Control Register */
#define SP_DP_LT_CTRL_REG		0xa8
#define SP_LT_ERROR_TYPE_MASK		0x70
#  define SP_LT_NO_ERROR		0x00
#  define SP_LT_AUX_WRITE_ERROR		0x01
#  define SP_LT_MAX_DRIVE_REACHED	0x02
#  define SP_LT_WRONG_LANE_COUNT_SET	0x03
#  define SP_LT_LOOP_SAME_5_TIME	0x04
#  define SP_LT_CR_FAIL_IN_EQ		0x05
#  define SP_LT_EQ_LOOP_5_TIME		0x06
#define SP_LT_EN			BIT(0)

/* DP CEP Training Control Registers */
#define SP_DP_CEP_TRAINING_CTRL0_REG	0xa9
#define SP_DP_CEP_TRAINING_CTRL1_REG	0xaa

/* DP Debug Register 1 */
#define SP_DP_DEBUG1_REG		0xb0
#define SP_DEBUG_PLL_LOCK		BIT(4)
#define SP_POLLING_EN			BIT(1)

/* DP Polling Control Register */
#define SP_DP_POLLING_CTRL_REG		0xb4
#define SP_AUTO_POLLING_DISABLE		BIT(0)

/* DP Link Debug Control Register */
#define SP_DP_LINK_DEBUG_CTRL_REG	0xb8
#define SP_M_VID_DEBUG			BIT(5)
#define SP_NEW_PRBS7			BIT(4)
#define SP_INSERT_ER			BIT(1)
#define SP_PRBS31_EN			BIT(0)

/* AUX Misc control Register */
#define SP_AUX_MISC_CTRL_REG		0xbf

/* DP PLL control Register */
#define SP_DP_PLL_CTRL_REG		0xc7
#define SP_PLL_RST			BIT(6)

/* DP Analog Power Down Register */
#define SP_DP_ANALOG_POWER_DOWN_REG	0xc8
#define SP_CH0_PD			BIT(0)

/* DP Misc Control Register */
#define SP_DP_MISC_CTRL_REG		0xcd
#define SP_EQ_TRAINING_LOOP		BIT(6)

/* DP Extra I2C Device Address Register */
#define SP_DP_EXTRA_I2C_DEV_ADDR_REG	0xce
#define SP_I2C_STRETCH_DISABLE		BIT(7)

#define SP_I2C_EXTRA_ADDR		0x50

/* DP Downspread Control Register 1 */
#define SP_DP_DOWNSPREAD_CTRL1_REG	0xd0

/* DP M Value Calculation Control Register */
#define SP_DP_M_CALCULATION_CTRL_REG	0xd9
#define SP_M_GEN_CLK_SEL		BIT(0)

/* AUX Channel Access Status Register */
#define SP_AUX_CH_STATUS_REG		0xe0
#define SP_AUX_STATUS			0x0f

/* AUX Channel DEFER Control Register */
#define SP_AUX_DEFER_CTRL_REG		0xe2
#define SP_DEFER_CTRL_EN		BIT(7)

/* DP Buffer Data Count Register */
#define SP_BUF_DATA_COUNT_REG		0xe4
#define SP_BUF_DATA_COUNT_MASK		0x1f
#define SP_BUF_CLR			BIT(7)

/* DP AUX Channel Control Register 1 */
#define SP_DP_AUX_CH_CTRL1_REG		0xe5
#define SP_AUX_TX_COMM_MASK		0x0f
#define SP_AUX_LENGTH_MASK		0xf0
#define SP_AUX_LENGTH_SHIFT		4

/* DP AUX CH Address Register 0 */
#define SP_AUX_ADDR_7_0_REG		0xe6

/* DP AUX CH Address Register 1 */
#define SP_AUX_ADDR_15_8_REG		0xe7

/* DP AUX CH Address Register 2 */
#define SP_AUX_ADDR_19_16_REG		0xe8
#define SP_AUX_ADDR_19_16_MASK		0x0f

/* DP AUX Channel Control Register 2 */
#define SP_DP_AUX_CH_CTRL2_REG		0xe9
#define SP_AUX_SEL_RXCM			BIT(6)
#define SP_AUX_CHSEL			BIT(3)
#define SP_AUX_PN_INV			BIT(2)
#define SP_ADDR_ONLY			BIT(1)
#define SP_AUX_EN			BIT(0)

/* DP Video Stream Control InfoFrame Register */
#define SP_DP_3D_VSC_CTRL_REG		0xea
#define SP_INFO_FRAME_VSC_EN		BIT(0)

/* DP Video Stream Data Byte 1 Register */
#define SP_DP_VSC_DB1_REG		0xeb

/* DP AUX Channel Control Register 3 */
#define SP_DP_AUX_CH_CTRL3_REG		0xec
#define SP_WAIT_COUNTER_7_0_MASK	0xff

/* DP AUX Channel Control Register 4 */
#define SP_DP_AUX_CH_CTRL4_REG		0xed

/* DP AUX Buffer Data Registers */
#define SP_DP_BUF_DATA0_REG		0xf0

/***************************************************************/
/* Register definition of device address 0x72                  */
/***************************************************************/

/*
 * Core Register Definitions
 */

/* Device ID Low Byte Register */
#define SP_DEVICE_IDL_REG		0x02

/* Device ID High Byte Register */
#define SP_DEVICE_IDH_REG		0x03

/* Device version register */
#define SP_DEVICE_VERSION_REG		0x04

/* Power Down Control Register */
#define SP_POWERDOWN_CTRL_REG		0x05
#define SP_REGISTER_PD			BIT(7)
#define SP_HDCP_PD			BIT(5)
#define SP_AUDIO_PD			BIT(4)
#define SP_VIDEO_PD			BIT(3)
#define SP_LINK_PD			BIT(2)
#define SP_TOTAL_PD			BIT(1)

/* Reset Control Register 1 */
#define SP_RESET_CTRL1_REG		0x06
#define SP_MISC_RST			BIT(7)
#define SP_VIDCAP_RST			BIT(6)
#define SP_VIDFIF_RST			BIT(5)
#define SP_AUDFIF_RST			BIT(4)
#define SP_AUDCAP_RST			BIT(3)
#define SP_HDCP_RST			BIT(2)
#define SP_SW_RST			BIT(1)
#define SP_HW_RST			BIT(0)

/* Reset Control Register 2 */
#define SP_RESET_CTRL2_REG		0x07
#define SP_AUX_RST			BIT(2)
#define SP_SERDES_FIFO_RST		BIT(1)
#define SP_I2C_REG_RST			BIT(0)

/* Video Control Register 1 */
#define SP_VID_CTRL1_REG		0x08
#define SP_VIDEO_EN			BIT(7)
#define SP_VIDEO_MUTE			BIT(2)
#define SP_DE_GEN			BIT(1)
#define SP_DEMUX			BIT(0)

/* Video Control Register 2 */
#define SP_VID_CTRL2_REG		0x09
#define SP_IN_COLOR_F_MASK		0x03
#define SP_IN_YC_BIT_SEL		BIT(2)
#define SP_IN_BPC_MASK			0x70
#define SP_IN_BPC_SHIFT			4
#  define SP_IN_BPC_12BIT		0x03
#  define SP_IN_BPC_10BIT		0x02
#  define SP_IN_BPC_8BIT		0x01
#  define SP_IN_BPC_6BIT		0x00
#define SP_IN_D_RANGE			BIT(7)

/* Video Control Register 3 */
#define SP_VID_CTRL3_REG		0x0a
#define SP_HPD_OUT			BIT(6)

/* Video Control Register 5 */
#define SP_VID_CTRL5_REG		0x0c
#define SP_CSC_STD_SEL			BIT(7)
#define SP_XVYCC_RNG_LMT		BIT(6)
#define SP_RANGE_Y2R			BIT(5)
#define SP_CSPACE_Y2R			BIT(4)
#define SP_RGB_RNG_LMT			BIT(3)
#define SP_Y_RNG_LMT			BIT(2)
#define SP_RANGE_R2Y			BIT(1)
#define SP_CSPACE_R2Y			BIT(0)

/* Video Control Register 6 */
#define SP_VID_CTRL6_REG		0x0d
#define SP_TEST_PATTERN_EN		BIT(7)
#define SP_VIDEO_PROCESS_EN		BIT(6)
#define SP_VID_US_MODE			BIT(3)
#define SP_VID_DS_MODE			BIT(2)
#define SP_UP_SAMPLE			BIT(1)
#define SP_DOWN_SAMPLE			BIT(0)

/* Video Control Register 8 */
#define SP_VID_CTRL8_REG		0x0f
#define SP_VID_VRES_TH			BIT(0)

/* Total Line Status Low Byte Register */
#define SP_TOTAL_LINE_STAL_REG		0x24

/* Total Line Status High Byte Register */
#define SP_TOTAL_LINE_STAH_REG		0x25

/* Active Line Status Low Byte Register */
#define SP_ACT_LINE_STAL_REG		0x26

/* Active Line Status High Byte Register */
#define SP_ACT_LINE_STAH_REG		0x27

/* Vertical Front Porch Status Register */
#define SP_V_F_PORCH_STA_REG		0x28

/* Vertical SYNC Width Status Register */
#define SP_V_SYNC_STA_REG		0x29

/* Vertical Back Porch Status Register */
#define SP_V_B_PORCH_STA_REG		0x2a

/* Total Pixel Status Low Byte Register */
#define SP_TOTAL_PIXEL_STAL_REG		0x2b

/* Total Pixel Status High Byte Register */
#define SP_TOTAL_PIXEL_STAH_REG		0x2c

/* Active Pixel Status Low Byte Register */
#define SP_ACT_PIXEL_STAL_REG		0x2d

/* Active Pixel Status High Byte Register */
#define SP_ACT_PIXEL_STAH_REG		0x2e

/* Horizontal Front Porch Status Low Byte Register */
#define SP_H_F_PORCH_STAL_REG		0x2f

/* Horizontal Front Porch Statys High Byte Register */
#define SP_H_F_PORCH_STAH_REG		0x30

/* Horizontal SYNC Width Status Low Byte Register */
#define SP_H_SYNC_STAL_REG		0x31

/* Horizontal SYNC Width Status High Byte Register */
#define SP_H_SYNC_STAH_REG		0x32

/* Horizontal Back Porch Status Low Byte Register */
#define SP_H_B_PORCH_STAL_REG		0x33

/* Horizontal Back Porch Status High Byte Register */
#define SP_H_B_PORCH_STAH_REG		0x34

/* InfoFrame AVI Packet DB1 Register */
#define SP_INFOFRAME_AVI_DB1_REG	0x70

/* Bit Control Specific Register */
#define SP_BIT_CTRL_SPECIFIC_REG	0x80
#define SP_BIT_CTRL_SELECT_SHIFT	1
#define SP_ENABLE_BIT_CTRL		BIT(0)

/* InfoFrame Audio Packet DB1 Register */
#define SP_INFOFRAME_AUD_DB1_REG	0x83

/* InfoFrame MPEG Packet DB1 Register */
#define SP_INFOFRAME_MPEG_DB1_REG	0xb0

/* Audio Channel Status Registers */
#define SP_AUD_CH_STATUS_BASE		0xd0

/* Audio Channel Num Register 5 */
#define SP_I2S_CHANNEL_NUM_MASK		0xe0
#  define SP_I2S_CH_NUM_1		(0x00 << 5)
#  define SP_I2S_CH_NUM_2		(0x01 << 5)
#  define SP_I2S_CH_NUM_3		(0x02 << 5)
#  define SP_I2S_CH_NUM_4		(0x03 << 5)
#  define SP_I2S_CH_NUM_5		(0x04 << 5)
#  define SP_I2S_CH_NUM_6		(0x05 << 5)
#  define SP_I2S_CH_NUM_7		(0x06 << 5)
#  define SP_I2S_CH_NUM_8		(0x07 << 5)
#define SP_EXT_VUCP			BIT(2)
#define SP_VBIT				BIT(1)
#define SP_AUDIO_LAYOUT			BIT(0)

/* Analog Debug Register 2 */
#define SP_ANALOG_DEBUG2_REG		0xdd
#define SP_FORCE_SW_OFF_BYPASS		0x20
#define SP_XTAL_FRQ			0x1c
#  define SP_XTAL_FRQ_19M2		(0x00 << 2)
#  define SP_XTAL_FRQ_24M		(0x01 << 2)
#  define SP_XTAL_FRQ_25M		(0x02 << 2)
#  define SP_XTAL_FRQ_26M		(0x03 << 2)
#  define SP_XTAL_FRQ_27M		(0x04 << 2)
#  define SP_XTAL_FRQ_38M4		(0x05 << 2)
#  define SP_XTAL_FRQ_52M		(0x06 << 2)
#define SP_POWERON_TIME_1P5MS		0x03

/* Analog Control 0 Register */
#define SP_ANALOG_CTRL0_REG		0xe1

/* Common Interrupt Status Register 1 */
#define SP_COMMON_INT_STATUS_BASE	(0xf1 - 1)
#define SP_PLL_LOCK_CHG			0x40

/* Common Interrupt Status Register 2 */
#define SP_COMMON_INT_STATUS2		0xf2
#define SP_HDCP_AUTH_CHG		BIT(1)
#define SP_HDCP_AUTH_DONE		BIT(0)

#define SP_HDCP_LINK_CHECK_FAIL		BIT(0)

/* Common Interrupt Status Register 4 */
#define SP_COMMON_INT_STATUS4_REG	0xf4
#define SP_HPD_IRQ			BIT(6)
#define SP_HPD_ESYNC_ERR		BIT(4)
#define SP_HPD_CHG			BIT(2)
#define SP_HPD_LOST			BIT(1)
#define SP_HPD_PLUG			BIT(0)

/* DP Interrupt Status Register */
#define SP_DP_INT_STATUS1_REG		0xf7
#define SP_TRAINING_FINISH		BIT(5)
#define SP_POLLING_ERR			BIT(4)

/* Common Interrupt Mask Register */
#define SP_COMMON_INT_MASK_BASE		(0xf8 - 1)

#define SP_COMMON_INT_MASK4_REG		0xfb

/* DP Interrupts Mask Register */
#define SP_DP_INT_MASK1_REG		0xfe

/* Interrupt Control Register */
#define SP_INT_CTRL_REG			0xff

/***************************************************************/
/* Register definition of device address 0x7a                  */
/***************************************************************/

/* DP TX Link Training Control Register */
#define SP_DP_TX_LT_CTRL0_REG		0x30

/* PD 1.2 Lint Training 80bit Pattern Register */
#define SP_DP_LT_80BIT_PATTERN0_REG	0x80
#define SP_DP_LT_80BIT_PATTERN_REG_NUM	10

/* Audio Interface Control Register 0 */
#define SP_AUD_INTERFACE_CTRL0_REG	0x5f
#define SP_AUD_INTERFACE_DISABLE	0x80

/* Audio Interface Control Register 2 */
#define SP_AUD_INTERFACE_CTRL2_REG	0x60
#define SP_M_AUD_ADJUST_ST		0x04

/* Audio Interface Control Register 3 */
#define SP_AUD_INTERFACE_CTRL3_REG	0x62

/* Audio Interface Control Register 4 */
#define SP_AUD_INTERFACE_CTRL4_REG	0x67

/* Audio Interface Control Register 5 */
#define SP_AUD_INTERFACE_CTRL5_REG	0x68

/* Audio Interface Control Register 6 */
#define SP_AUD_INTERFACE_CTRL6_REG	0x69

/* Firmware Version Register */
#define SP_FW_VER_REG			0xb7

#endif

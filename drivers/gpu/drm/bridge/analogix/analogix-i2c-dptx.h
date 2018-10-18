/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2017 Icenowy Zheng <icenowy@aosc.io>
 *
 * Based on analogix-anx78xx.h, which is:
 *   Copyright(c) 2016, Analogix Semiconductor. All rights reserved.
 */

#ifndef _ANALOGIX_I2C_DPTX_H_
#define _ANALOGIX_I2C_DPTX_H_

#define ANALOGIX_I2C_DPTX		0x70

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
#define SP_HPD_FORCE			BIT(5)
#define SP_HPD_CTRL			BIT(4)
#define SP_STRM_VALID			BIT(2)
#define SP_STRM_FORCE			BIT(1)
#define SP_STRM_CTRL			BIT(0)
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

/* DP Lane Count Setting Register */
#define SP_DP_LANE_COUNT_SET_REG	0xa1

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
#define SP_DP_LT_INPROGRESS		0x80
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

ssize_t anx_aux_transfer(struct regmap *map_dptx, struct drm_dp_aux_msg *msg);

#endif

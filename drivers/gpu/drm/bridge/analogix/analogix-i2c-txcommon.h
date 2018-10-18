/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2017 Icenowy Zheng <icenowy@aosc.io>
 *
 * Based on analogix-anx78xx.h, which is:
 *   Copyright(c) 2016, Analogix Semiconductor. All rights reserved.
 */

#ifndef _ANALOGIX_I2C_TXCOMMON_H_
#define _ANALOGIX_I2C_TXCOMMON_H_

#define ANALOGIX_I2C_TXCOMMON		0x72

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

/* Analog Debug Register 1 */
#define SP_ANALOG_DEBUG1_REG		0xdc

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

#endif /* _ANALOGIX_I2C_TXCOMMON_H_ */

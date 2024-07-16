/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2016, Analogix Semiconductor. All rights reserved.
 */

#ifndef __ANX78xx_H
#define __ANX78xx_H

#include "analogix-i2c-dptx.h"
#include "analogix-i2c-txcommon.h"

/***************************************************************/
/* Register definitions for RX_PO                              */
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
/* Register definitions for RX_P1                              */
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
/* Register definitions for TX_P1                              */
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

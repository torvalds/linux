/*
 * drivers/video/tegra/dc/hdmi_reg.h
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DRIVERS_VIDEO_TEGRA_DC_HDMI_REG_H
#define __DRIVERS_VIDEO_TEGRA_DC_HDMI_REG_H

#define HDMI_CTXSW						0x00
#define HDMI_NV_PDISP_SOR_STATE0				0x01
#define  SOR_STATE_UPDATE			(1 << 0)

#define HDMI_NV_PDISP_SOR_STATE1				0x02
#define  SOR_STATE_ASY_HEAD_OPMODE_SLEEP	(0 << 0)
#define  SOR_STATE_ASY_HEAD_OPMODE_SNOOSE	(1 << 0)
#define  SOR_STATE_ASY_HEAD_OPMODE_AWAKE	(2 << 0)
#define  SOR_STATE_ASY_ORMODE_SAFE		(0 << 2)
#define  SOR_STATE_ASY_ORMODE_NORMAL		(1 << 2)
#define  SOR_STATE_ATTACHED			(1 << 3)
#define  SOR_STATE_ARM_SHOW_VGA			(1 << 4)

#define HDMI_NV_PDISP_SOR_STATE2				0x03
#define  SOR_STATE_ASY_OWNER_NONE		(0 << 0)
#define  SOR_STATE_ASY_OWNER_HEAD0		(1 << 0)
#define  SOR_STATE_ASY_SUBOWNER_NONE		(0 << 4)
#define  SOR_STATE_ASY_SUBOWNER_SUBHEAD0	(1 << 4)
#define  SOR_STATE_ASY_SUBOWNER_SUBHEAD1	(2 << 4)
#define  SOR_STATE_ASY_SUBOWNER_BOTH		(3 << 4)
#define  SOR_STATE_ASY_CRCMODE_ACTIVE		(0 << 6)
#define  SOR_STATE_ASY_CRCMODE_COMPLETE		(1 << 6)
#define  SOR_STATE_ASY_CRCMODE_NON_ACTIVE	(2 << 6)
#define  SOR_STATE_ASY_PROTOCOL_SINGLE_TMDS_A	(1 << 8)
#define  SOR_STATE_ASY_PROTOCOL_CUSTOM		(15 << 8)
#define  SOR_STATE_ASY_HSYNCPOL_POS		(0 << 12)
#define  SOR_STATE_ASY_HSYNCPOL_NEG		(1 << 12)
#define  SOR_STATE_ASY_VSYNCPOL_POS		(0 << 13)
#define  SOR_STATE_ASY_VSYNCPOL_NEG		(1 << 13)
#define  SOR_STATE_ASY_DEPOL_POS		(0 << 14)
#define  SOR_STATE_ASY_DEPOL_NEG		(1 << 14)

#define HDMI_NV_PDISP_RG_HDCP_AN_MSB				0x04
#define HDMI_NV_PDISP_RG_HDCP_AN_LSB				0x05
#define HDMI_NV_PDISP_RG_HDCP_CN_MSB				0x06
#define HDMI_NV_PDISP_RG_HDCP_CN_LSB				0x07
#define HDMI_NV_PDISP_RG_HDCP_AKSV_MSB				0x08
#define HDMI_NV_PDISP_RG_HDCP_AKSV_LSB				0x09
#define HDMI_NV_PDISP_RG_HDCP_BKSV_MSB				0x0a
#define  REPEATER				(1 << 31)
#define HDMI_NV_PDISP_RG_HDCP_BKSV_LSB				0x0b
#define HDMI_NV_PDISP_RG_HDCP_CKSV_MSB				0x0c
#define HDMI_NV_PDISP_RG_HDCP_CKSV_LSB				0x0d
#define HDMI_NV_PDISP_RG_HDCP_DKSV_MSB				0x0e
#define HDMI_NV_PDISP_RG_HDCP_DKSV_LSB				0x0f
#define HDMI_NV_PDISP_RG_HDCP_CTRL				0x10
#define  HDCP_RUN_YES				(1 << 0)
#define  CRYPT_ENABLED				(1 << 1)
#define  ONEONE_ENABLED				(1 << 3)
#define  AN_VALID				(1 << 8)
#define  R0_VALID				(1 << 9)
#define  SPRIME_VALID				(1 << 10)
#define  MPRIME_VALID				(1 << 11)
#define  SROM_ERR				(1 << 13)
#define HDMI_NV_PDISP_RG_HDCP_CMODE				0x11
#define  TMDS0_LINK0				(1 << 4)
#define  READ_S					(1 << 0)
#define  READ_M					(2 << 0)
#define HDMI_NV_PDISP_RG_HDCP_MPRIME_MSB			0x12
#define HDMI_NV_PDISP_RG_HDCP_MPRIME_LSB			0x13
#define HDMI_NV_PDISP_RG_HDCP_SPRIME_MSB			0x14
#define  STATUS_CS				(1 << 6)
#define HDMI_NV_PDISP_RG_HDCP_SPRIME_LSB2			0x15
#define HDMI_NV_PDISP_RG_HDCP_SPRIME_LSB1			0x16
#define HDMI_NV_PDISP_RG_HDCP_RI				0x17
#define HDMI_NV_PDISP_RG_HDCP_CS_MSB				0x18
#define HDMI_NV_PDISP_RG_HDCP_CS_LSB				0x19
#define HDMI_NV_PDISP_HDMI_AUDIO_EMU0				0x1a
#define HDMI_NV_PDISP_HDMI_AUDIO_EMU_RDATA0			0x1b
#define HDMI_NV_PDISP_HDMI_AUDIO_EMU1				0x1c
#define HDMI_NV_PDISP_HDMI_AUDIO_EMU2				0x1d
#define HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL			0x1e
#define HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_STATUS		0x1f
#define HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_HEADER		0x20
#define HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_LOW		0x21
#define HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_HIGH	0x22
#define HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL			0x23
#define HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_STATUS			0x24
#define HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_HEADER			0x25
#define HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_LOW		0x26
#define HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_HIGH		0x27
#define HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_LOW		0x28
#define HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_HIGH		0x29
#define  INFOFRAME_CTRL_ENABLE			(1 << 0)
#define  INFOFRAME_CTRL_OTHER			(1 << 4)
#define  INFOFRAME_CTRL_SINGLE			(1 << 8)

#define INFOFRAME_HEADER_TYPE(x)		((x) & 0xff)
#define INFOFRAME_HEADER_VERSION(x)		(((x) & 0xff) << 8)
#define INFOFRAME_HEADER_LEN(x)			(((x) & 0xf) << 16)

#define HDMI_NV_PDISP_HDMI_GENERIC_CTRL				0x2a
#define  GENERIC_CTRL_ENABLE			(1 << 0)
#define  GENERIC_CTRL_OTHER			(1 << 4)
#define  GENERIC_CTRL_SINGLE			(1 << 8)
#define  GENERIC_CTRL_HBLANK			(1 << 12)
#define  GENERIC_CTRL_AUDIO			(1 << 16)

#define HDMI_NV_PDISP_HDMI_GENERIC_STATUS			0x2b
#define HDMI_NV_PDISP_HDMI_GENERIC_HEADER			0x2c
#define HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK0_LOW			0x2d
#define HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK0_HIGH		0x2e
#define HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK1_LOW			0x2f
#define HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK1_HIGH		0x30
#define HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK2_LOW			0x31
#define HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK2_HIGH		0x32
#define HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK3_LOW			0x33
#define HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK3_HIGH		0x34
#define HDMI_NV_PDISP_HDMI_ACR_CTRL				0x35
#define HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_LOW			0x36
#define HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_HIGH		0x37
#define HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_LOW			0x38
#define HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_HIGH		0x39
#define HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_LOW			0x3a
#define HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_HIGH		0x3b
#define HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_LOW			0x3c
#define HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_HIGH		0x3d
#define HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_LOW			0x3e
#define HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_HIGH		0x3f
#define HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_LOW			0x40
#define HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_HIGH		0x41
#define HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_LOW			0x42
#define HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_HIGH		0x43
#define  ACR_SB3(x)				(((x) & 0xff) << 8)
#define  ACR_SB2(x)				(((x) & 0xff) << 16)
#define  ACR_SB1(x)				(((x) & 0xff) << 24)
#define  ACR_SUBPACK_CTS(x)			(((x) & 0xffffff) << 8)

#define  ACR_SB6(x)				(((x) & 0xff) << 0)
#define  ACR_SB5(x)				(((x) & 0xff) << 8)
#define  ACR_SB4(x)				(((x) & 0xff) << 16)
#define  ACR_ENABLE				(1 << 31)
#define  ACR_SUBPACK_N(x)			((x) & 0xffffff)

#define HDMI_NV_PDISP_HDMI_CTRL					0x44
#define  HDMI_CTRL_REKEY(x)			(((x) & 0x7f) << 0)
#define  HDMI_CTRL_AUDIO_LAYOUT			(1 << 8)
#define  HDMI_CTRL_SAMPLE_FLAT			(1 << 12)
#define  HDMI_CTRL_MAX_AC_PACKET(x)		(((x) & 0x1f) << 16)
#define  HDMI_CTRL_ENABLE			(1 << 30)

#define HDMI_NV_PDISP_HDMI_VSYNC_KEEPOUT			0x45
#define HDMI_NV_PDISP_HDMI_VSYNC_WINDOW				0x46
#define  VSYNC_WINDOW_END(x)			(((x) & 0x3ff) << 0)
#define  VSYNC_WINDOW_START(x)			(((x) & 0x3ff) << 16)
#define  VSYNC_WINDOW_ENABLE			(1 << 31)

#define HDMI_NV_PDISP_HDMI_GCP_CTRL				0x47
#define HDMI_NV_PDISP_HDMI_GCP_STATUS				0x48
#define HDMI_NV_PDISP_HDMI_GCP_SUBPACK				0x49
#define HDMI_NV_PDISP_HDMI_CHANNEL_STATUS1			0x4a
#define HDMI_NV_PDISP_HDMI_CHANNEL_STATUS2			0x4b
#define HDMI_NV_PDISP_HDMI_EMU0					0x4c
#define HDMI_NV_PDISP_HDMI_EMU1					0x4d
#define HDMI_NV_PDISP_HDMI_EMU1_RDATA				0x4e
#define HDMI_NV_PDISP_HDMI_SPARE				0x4f
#define  SPARE_HW_CTS				(1 << 0)
#define  SPARE_FORCE_SW_CTS			(1 << 1)
#define  SPARE_CTS_RESET_VAL(x)			(((x) & 0x7) << 16)
#define  SPARE_ACR_PRIORITY_HIGH		(0 << 31)
#define  SPARE_ACR_PRIORITY_LOW			(1 << 31)

#define HDMI_NV_PDISP_HDMI_SPDIF_CHN_STATUS1			0x50
#define HDMI_NV_PDISP_HDMI_SPDIF_CHN_STATUS2			0x51
#define HDMI_NV_PDISP_HDCPRIF_ROM_CTRL				0x53
#define HDMI_NV_PDISP_SOR_CAP					0x54
#define HDMI_NV_PDISP_SOR_PWR					0x55
#define  SOR_PWR_NORMAL_STATE_PD		(0 << 0)
#define  SOR_PWR_NORMAL_STATE_PU		(1 << 0)
#define  SOR_PWR_NORMAL_START_NORMAL		(0 << 1)
#define  SOR_PWR_NORMAL_START_ALT		(1 << 1)
#define  SOR_PWR_SAFE_STATE_PD			(0 << 16)
#define  SOR_PWR_SAFE_STATE_PU			(1 << 16)
#define  SOR_PWR_SAFE_START_NORMAL		(0 << 17)
#define  SOR_PWR_SAFE_START_ALT			(1 << 17)
#define  SOR_PWR_HALT_DELAY			(1 << 24)
#define  SOR_PWR_MODE				(1 << 28)
#define  SOR_PWR_SETTING_NEW_DONE		(0 << 31)
#define  SOR_PWR_SETTING_NEW_PENDING		(1 << 31)
#define  SOR_PWR_SETTING_NEW_TRIGGER		(1 << 31)

#define HDMI_NV_PDISP_SOR_TEST					0x56
#define HDMI_NV_PDISP_SOR_PLL0					0x57
#define  SOR_PLL_PWR				(1 << 0)
#define  SOR_PLL_PDBG				(1 << 1)
#define  SOR_PLL_VCOPD				(1 << 2)
#define  SOR_PLL_PDPORT				(1 << 3)
#define  SOR_PLL_RESISTORSEL			(1 << 4)
#define  SOR_PLL_PULLDOWN			(1 << 5)
#define  SOR_PLL_VCOCAP(x)			(((x) & 0xf) << 8)
#define  SOR_PLL_BG_V17_S(x)			(((x) & 0xf) << 12)
#define  SOR_PLL_FILTER(x)			(((x) & 0xf) << 16)
#define  SOR_PLL_ICHPMP(x)			(((x) & 0xf) << 24)
#define  SOR_PLL_TX_REG_LOAD(x)			(((x) & 0x3) << 28)

#define HDMI_NV_PDISP_SOR_PLL1					0x58
#define  SOR_PLL_TMDS_TERM_ENABLE		(1 << 8)
#define  SOR_PLL_TMDS_TERMADJ(x)		(((x) & 0xf) << 9)
#define  SOR_PLL_LOADADJ(x)			(((x) & 0xf) << 20)
#define  SOR_PLL_PE_EN				(1 << 28)
#define  SOR_PLL_HALF_FULL_PE			(1 << 29)
#define  SOR_PLL_S_D_PIN_PE			(1 << 30)

#define HDMI_NV_PDISP_SOR_PLL2					0x59
#define HDMI_NV_PDISP_SOR_CSTM					0x5a
#define  SOR_CSTM_PD_TXDA_0			(1 << 0)
#define  SOR_CSTM_PD_TXDA_1			(1 << 1)
#define  SOR_CSTM_PD_TXDA_2			(1 << 2)
#define  SOR_CSTM_PD_TXDA_3			(1 << 3)
#define  SOR_CSTM_PD_TXDB_0			(1 << 4)
#define  SOR_CSTM_PD_TXDB_1			(1 << 5)
#define  SOR_CSTM_PD_TXDB_2			(1 << 6)
#define  SOR_CSTM_PD_TXDB_3			(1 << 7)
#define  SOR_CSTM_PD_TXCA			(1 << 8)
#define  SOR_CSTM_PD_TXCB			(1 << 9)
#define  SOR_CSTM_UPPER				(1 << 11)
#define  SOR_CSTM_MODE(x)			(((x) & 0x3) << 12)
#define  SOR_CSTM_LINKACTA			(1 << 14)
#define  SOR_CSTM_LINKACTB			(1 << 15)
#define  SOR_CSTM_LVDS_EN			(1 << 16)
#define  SOR_CSTM_DUP_SYNC			(1 << 17)
#define  SOR_CSTM_NEW_MODE			(1 << 18)
#define  SOR_CSTM_BALANCED			(1 << 19)
#define  SOR_CSTM_PLLDIV			(1 << 21)
#define  SOR_CSTM_ROTCLK(x)			(((x) & 0xf) << 24)
#define  SOR_CSTM_ROTDAT(x)			(((x) & 0x7) << 28)

#define HDMI_NV_PDISP_SOR_LVDS					0x5b
#define HDMI_NV_PDISP_SOR_CRCA					0x5c
#define HDMI_NV_PDISP_SOR_CRCB					0x5d
#define HDMI_NV_PDISP_SOR_BLANK					0x5e
#define HDMI_NV_PDISP_SOR_SEQ_CTL				0x5f
#define  SOR_SEQ_CTL_PU_PC(x)			(((x) & 0xf) << 0)
#define  SOR_SEQ_PU_PC_ALT(x)			(((x) & 0xf) << 4)
#define  SOR_SEQ_PD_PC(x)			(((x) & 0xf) << 8)
#define  SOR_SEQ_PD_PC_ALT(x)			(((x) & 0xf) << 12)
#define  SOR_SEQ_PC(x)				(((x) & 0xf) << 16)
#define  SOR_SEQ_STATUS				(1 << 28)
#define  SOR_SEQ_SWITCH				(1 << 30)

#define HDMI_NV_PDISP_SOR_SEQ_INST0				0x60
#define HDMI_NV_PDISP_SOR_SEQ_INST1				0x61
#define HDMI_NV_PDISP_SOR_SEQ_INST2				0x62
#define HDMI_NV_PDISP_SOR_SEQ_INST3				0x63
#define HDMI_NV_PDISP_SOR_SEQ_INST4				0x64
#define HDMI_NV_PDISP_SOR_SEQ_INST5				0x65
#define HDMI_NV_PDISP_SOR_SEQ_INST6				0x66
#define HDMI_NV_PDISP_SOR_SEQ_INST7				0x67
#define HDMI_NV_PDISP_SOR_SEQ_INST8				0x68
#define HDMI_NV_PDISP_SOR_SEQ_INST9				0x69
#define HDMI_NV_PDISP_SOR_SEQ_INSTA				0x6a
#define HDMI_NV_PDISP_SOR_SEQ_INSTB				0x6b
#define HDMI_NV_PDISP_SOR_SEQ_INSTC				0x6c
#define HDMI_NV_PDISP_SOR_SEQ_INSTD				0x6d
#define HDMI_NV_PDISP_SOR_SEQ_INSTE				0x6e
#define HDMI_NV_PDISP_SOR_SEQ_INSTF				0x6f
#define  SOR_SEQ_INST_WAIT_TIME(x)		(((x) & 0x3ff) << 0)
#define  SOR_SEQ_INST_WAIT_UNITS_US		(0 << 12)
#define  SOR_SEQ_INST_WAIT_UNITS_MS		(1 << 12)
#define  SOR_SEQ_INST_WAIT_UNITS_VSYNC		(2 << 12)
#define  SOR_SEQ_INST_HALT			(1 << 15)
#define  SOR_SEQ_INST_PIN_A_LOW			(0 << 21)
#define  SOR_SEQ_INST_PIN_A_HIGH		(1 << 21)
#define  SOR_SEQ_INST_PIN_B_LOW			(0 << 22)
#define  SOR_SEQ_INST_PIN_B_HIGH		(1 << 22)
#define  SOR_SEQ_INST_DRIVE_PWM_OUT_LO		(1 << 23)
#define  SOR_SEQ_INST_TRISTATE_IOS		(1 << 24)
#define  SOR_SEQ_INST_SOR_SEQ_INST_BLACK_DATA	(1 << 25)
#define  SOR_SEQ_INST_BLANK_DE			(1 << 26)
#define  SOR_SEQ_INST_BLANK_H			(1 << 27)
#define  SOR_SEQ_INST_BLANK_V			(1 << 28)
#define  SOR_SEQ_INST_ASSERT_PLL_RESETV		(1 << 29)
#define  SOR_SEQ_INST_POWERDOWN_MACRO		(1 << 30)
#define  SOR_SEQ_INST_PLL_PULLDOWN		(1 << 31)

#define HDMI_NV_PDISP_SOR_VCRCA0				0x72
#define HDMI_NV_PDISP_SOR_VCRCA1				0x73
#define HDMI_NV_PDISP_SOR_CCRCA0				0x74
#define HDMI_NV_PDISP_SOR_CCRCA1				0x75
#define HDMI_NV_PDISP_SOR_EDATAA0				0x76
#define HDMI_NV_PDISP_SOR_EDATAA1				0x77
#define HDMI_NV_PDISP_SOR_COUNTA0				0x78
#define HDMI_NV_PDISP_SOR_COUNTA1				0x79
#define HDMI_NV_PDISP_SOR_DEBUGA0				0x7a
#define HDMI_NV_PDISP_SOR_DEBUGA1				0x7b
#define HDMI_NV_PDISP_SOR_TRIG					0x7c
#define HDMI_NV_PDISP_SOR_MSCHECK				0x7d
#define HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT			0x7e
#define  DRIVE_CURRENT_LANE0(x)			(((x) & 0x3f) << 0)
#define  DRIVE_CURRENT_LANE1(x)			(((x) & 0x3f) << 8)
#define  DRIVE_CURRENT_LANE2(x)			(((x) & 0x3f) << 16)
#define  DRIVE_CURRENT_LANE3(x)			(((x) & 0x3f) << 24)
#define  DRIVE_CURRENT_FUSE_OVERRIDE		(1 << 31)
#define  DRIVE_CURRENT_1_500_mA			0x00
#define  DRIVE_CURRENT_1_875_mA			0x01
#define  DRIVE_CURRENT_2_250_mA			0x02
#define  DRIVE_CURRENT_2_625_mA			0x03
#define  DRIVE_CURRENT_3_000_mA			0x04
#define  DRIVE_CURRENT_3_375_mA			0x05
#define  DRIVE_CURRENT_3_750_mA			0x06
#define  DRIVE_CURRENT_4_125_mA			0x07
#define  DRIVE_CURRENT_4_500_mA			0x08
#define  DRIVE_CURRENT_4_875_mA			0x09
#define  DRIVE_CURRENT_5_250_mA			0x0a
#define  DRIVE_CURRENT_5_625_mA			0x0b
#define  DRIVE_CURRENT_6_000_mA			0x0c
#define  DRIVE_CURRENT_6_375_mA			0x0d
#define  DRIVE_CURRENT_6_750_mA			0x0e
#define  DRIVE_CURRENT_7_125_mA			0x0f
#define  DRIVE_CURRENT_7_500_mA			0x10
#define  DRIVE_CURRENT_7_875_mA			0x11
#define  DRIVE_CURRENT_8_250_mA			0x12
#define  DRIVE_CURRENT_8_625_mA			0x13
#define  DRIVE_CURRENT_9_000_mA			0x14
#define  DRIVE_CURRENT_9_375_mA			0x15
#define  DRIVE_CURRENT_9_750_mA			0x16
#define  DRIVE_CURRENT_10_125_mA		0x17
#define  DRIVE_CURRENT_10_500_mA		0x18
#define  DRIVE_CURRENT_10_875_mA		0x19
#define  DRIVE_CURRENT_11_250_mA		0x1a
#define  DRIVE_CURRENT_11_625_mA		0x1b
#define  DRIVE_CURRENT_12_000_mA		0x1c
#define  DRIVE_CURRENT_12_375_mA		0x1d
#define  DRIVE_CURRENT_12_750_mA		0x1e
#define  DRIVE_CURRENT_13_125_mA		0x1f
#define  DRIVE_CURRENT_13_500_mA		0x20
#define  DRIVE_CURRENT_13_875_mA		0x21
#define  DRIVE_CURRENT_14_250_mA		0x22
#define  DRIVE_CURRENT_14_625_mA		0x23
#define  DRIVE_CURRENT_15_000_mA		0x24
#define  DRIVE_CURRENT_15_375_mA		0x25
#define  DRIVE_CURRENT_15_750_mA		0x26
#define  DRIVE_CURRENT_16_125_mA		0x27
#define  DRIVE_CURRENT_16_500_mA		0x28
#define  DRIVE_CURRENT_16_875_mA		0x29
#define  DRIVE_CURRENT_17_250_mA		0x2a
#define  DRIVE_CURRENT_17_625_mA		0x2b
#define  DRIVE_CURRENT_18_000_mA		0x2c
#define  DRIVE_CURRENT_18_375_mA		0x2d
#define  DRIVE_CURRENT_18_750_mA		0x2e
#define  DRIVE_CURRENT_19_125_mA		0x2f
#define  DRIVE_CURRENT_19_500_mA		0x30
#define  DRIVE_CURRENT_19_875_mA		0x31
#define  DRIVE_CURRENT_20_250_mA		0x32
#define  DRIVE_CURRENT_20_625_mA		0x33
#define  DRIVE_CURRENT_21_000_mA		0x34
#define  DRIVE_CURRENT_21_375_mA		0x35
#define  DRIVE_CURRENT_21_750_mA		0x36
#define  DRIVE_CURRENT_22_125_mA		0x37
#define  DRIVE_CURRENT_22_500_mA		0x38
#define  DRIVE_CURRENT_22_875_mA		0x39
#define  DRIVE_CURRENT_23_250_mA		0x3a
#define  DRIVE_CURRENT_23_625_mA		0x3b
#define  DRIVE_CURRENT_24_000_mA		0x3c
#define  DRIVE_CURRENT_24_375_mA		0x3d
#define  DRIVE_CURRENT_24_750_mA		0x3e

#define HDMI_NV_PDISP_AUDIO_DEBUG0				0x7f
#define HDMI_NV_PDISP_AUDIO_DEBUG1				0x80
#define HDMI_NV_PDISP_AUDIO_DEBUG2				0x81
/* note: datasheet defines FS1..FS7.  we have FS(0)..FS(6) */
#define HDMI_NV_PDISP_AUDIO_FS(x)				(0x82 + (x))
#define  AUDIO_FS_LOW(x)			(((x) & 0xfff) << 0)
#define  AUDIO_FS_HIGH(x)			(((x) & 0xfff) << 16)


#define HDMI_NV_PDISP_AUDIO_PULSE_WIDTH				0x89
#define HDMI_NV_PDISP_AUDIO_THRESHOLD				0x8a
#define HDMI_NV_PDISP_AUDIO_CNTRL0				0x8b
#define  AUDIO_CNTRL0_ERROR_TOLERANCE(x)	(((x) & 0xff) << 0)
#define  AUDIO_CNTRL0_SOFT_RESET		(1 << 8)
#define  AUDIO_CNTRL0_SOFT_RESET_ALL		(1 << 12)
#define  AUDIO_CNTRL0_SAMPLING_FREQ_UNKNOWN	(1 << 16)
#define  AUDIO_CNTRL0_SAMPLING_FREQ_32K		(2 << 16)
#define  AUDIO_CNTRL0_SAMPLING_FREQ_44_1K	(0 << 16)
#define  AUDIO_CNTRL0_SAMPLING_FREQ_48K		(2 << 16)
#define  AUDIO_CNTRL0_SAMPLING_FREQ_88_2K	(8 << 16)
#define  AUDIO_CNTRL0_SAMPLING_FREQ_96K		(10 << 16)
#define  AUDIO_CNTRL0_SAMPLING_FREQ_176_4K	(12 << 16)
#define  AUDIO_CNTRL0_SAMPLING_FREQ_192K	(14 << 16)
#define  AUDIO_CNTRL0_SOURCE_SELECT_AUTO	(0 << 20)
#define  AUDIO_CNTRL0_SOURCE_SELECT_SPDIF	(1 << 20)
#define  AUDIO_CNTRL0_SOURCE_SELECT_HDAL	(2 << 20)
#define  AUDIO_CNTRL0_FRAMES_PER_BLOCK(x)	(((x) & 0xff) << 24)

#define HDMI_NV_PDISP_AUDIO_N					0x8c
#define  AUDIO_N_VALUE(x)			(((x) & 0xfffff) << 0)
#define  AUDIO_N_RESETF				(1 << 20)
#define  AUDIO_N_GENERATE_NORMAL		(0 << 24)
#define  AUDIO_N_GENERATE_ALTERNALTE		(1 << 24)
#define  AUDIO_N_LOOKUP_ENABLE			(1 << 28)

#define HDMI_NV_PDISP_HDCPRIF_ROM_TIMING			0x94
#define HDMI_NV_PDISP_SOR_REFCLK				0x95
#define  SOR_REFCLK_DIV_INT(x)			(((x) & 0xff) << 8)
#define  SOR_REFCLK_DIV_FRAC(x)			(((x) & 0x3) << 6)

#define HDMI_NV_PDISP_CRC_CONTROL				0x96
#define HDMI_NV_PDISP_INPUT_CONTROL				0x97
#define  HDMI_SRC_DISPLAYA			(0 << 0)
#define  HDMI_SRC_DISPLAYB			(1 << 0)
#define  ARM_VIDEO_RANGE_FULL			(0 << 1)
#define  ARM_VIDEO_RANGE_LIMITED		(1 << 1)

#define HDMI_NV_PDISP_SCRATCH					0x98
#define HDMI_NV_PDISP_PE_CURRENT				0x99
#define  PE_CURRENT0(x)				(((x) & 0xf) << 0)
#define  PE_CURRENT1(x)				(((x) & 0xf) << 8)
#define  PE_CURRENT2(x)				(((x) & 0xf) << 16)
#define  PE_CURRENT3(x)				(((x) & 0xf) << 24)

#define HDMI_NV_PDISP_KEY_CTRL					0x9a
#define  LOCAL_KEYS				(1 << 0)
#define  AUTOINC				(1 << 1)
#define  WRITE16				(1 << 4)
#define  PKEY_REQUEST_RELOAD_TRIGGER		(1 << 5)
#define  PKEY_LOADED				(1 << 6)
#define HDMI_NV_PDISP_KEY_DEBUG0				0x9b
#define HDMI_NV_PDISP_KEY_DEBUG1				0x9c
#define HDMI_NV_PDISP_KEY_DEBUG2				0x9d
#define HDMI_NV_PDISP_KEY_HDCP_KEY_0				0x9e
#define HDMI_NV_PDISP_KEY_HDCP_KEY_1				0x9f
#define HDMI_NV_PDISP_KEY_HDCP_KEY_2				0xa0
#define HDMI_NV_PDISP_KEY_HDCP_KEY_3				0xa1
#define HDMI_NV_PDISP_KEY_HDCP_KEY_TRIG				0xa2
#define HDMI_NV_PDISP_KEY_SKEY_INDEX				0xa3

#endif

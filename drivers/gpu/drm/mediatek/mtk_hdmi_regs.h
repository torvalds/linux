/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _MTK_HDMI_REGS_H
#define _MTK_HDMI_REGS_H

#define GRL_INT_MASK		0x18
#define GRL_IFM_PORT		0x188
#define GRL_CH_SWAP		0x198
#define LR_SWAP				BIT(0)
#define LFE_CC_SWAP			BIT(1)
#define LSRS_SWAP			BIT(2)
#define RLS_RRS_SWAP			BIT(3)
#define LR_STATUS_SWAP			BIT(4)
#define GRL_I2S_C_STA0		0x140
#define GRL_I2S_C_STA1		0x144
#define GRL_I2S_C_STA2		0x148
#define GRL_I2S_C_STA3		0x14C
#define GRL_I2S_C_STA4		0x150
#define GRL_I2S_UV		0x154
#define I2S_UV_V			BIT(0)
#define I2S_UV_U			BIT(1)
#define I2S_UV_CH_EN_MASK		0x3c
#define I2S_UV_CH_EN(x)			BIT((x) + 2)
#define I2S_UV_TMDS_DEBUG		BIT(6)
#define I2S_UV_NORMAL_INFO_INV		BIT(7)
#define GRL_ACP_ISRC_CTRL	0x158
#define VS_EN				BIT(0)
#define ACP_EN				BIT(1)
#define ISRC1_EN			BIT(2)
#define ISRC2_EN			BIT(3)
#define GAMUT_EN			BIT(4)
#define GRL_CTS_CTRL		0x160
#define CTS_CTRL_SOFT			BIT(0)
#define GRL_INT			0x14
#define INT_MDI				BIT(0)
#define INT_HDCP			BIT(1)
#define INT_FIFO_O			BIT(2)
#define INT_FIFO_U			BIT(3)
#define INT_IFM_ERR			BIT(4)
#define INT_INF_DONE			BIT(5)
#define INT_NCTS_DONE			BIT(6)
#define INT_CTRL_PKT_DONE		BIT(7)
#define GRL_INT_MASK		0x18
#define GRL_CTRL		0x1C
#define CTRL_GEN_EN			BIT(2)
#define CTRL_SPD_EN			BIT(3)
#define CTRL_MPEG_EN			BIT(4)
#define CTRL_AUDIO_EN			BIT(5)
#define CTRL_AVI_EN			BIT(6)
#define CTRL_AVMUTE			BIT(7)
#define	GRL_STATUS		0x20
#define STATUS_HTPLG			BIT(0)
#define STATUS_PORD			BIT(1)
#define GRL_DIVN		0x170
#define NCTS_WRI_ANYTIME		BIT(6)
#define GRL_AUDIO_CFG		0x17C
#define AUDIO_ZERO			BIT(0)
#define HIGH_BIT_RATE			BIT(1)
#define SACD_DST			BIT(2)
#define DST_NORMAL_DOUBLE		BIT(3)
#define DSD_INV				BIT(4)
#define LR_INV				BIT(5)
#define LR_MIX				BIT(6)
#define DSD_SEL				BIT(7)
#define GRL_NCTS		0x184
#define GRL_CH_SW0		0x18C
#define GRL_CH_SW1		0x190
#define GRL_CH_SW2		0x194
#define CH_SWITCH(from, to)		((from) << ((to) * 3))
#define GRL_INFOFRM_VER		0x19C
#define GRL_INFOFRM_TYPE	0x1A0
#define GRL_INFOFRM_LNG		0x1A4
#define GRL_MIX_CTRL		0x1B4
#define MIX_CTRL_SRC_EN			BIT(0)
#define BYPASS_VOLUME			BIT(1)
#define MIX_CTRL_FLAT			BIT(7)
#define GRL_AOUT_CFG		0x1C4
#define AOUT_BNUM_SEL_MASK		0x03
#define AOUT_24BIT			0x00
#define AOUT_20BIT			0x02
#define AOUT_16BIT			0x03
#define AOUT_FIFO_ADAP_CTRL		BIT(6)
#define AOUT_BURST_PREAMBLE_EN		BIT(7)
#define HIGH_BIT_RATE_PACKET_ALIGN	(AOUT_BURST_PREAMBLE_EN | \
					 AOUT_FIFO_ADAP_CTRL)
#define GRL_SHIFT_L1		0x1C0
#define GRL_SHIFT_R2		0x1B0
#define AUDIO_PACKET_OFF		BIT(6)
#define GRL_CFG0		0x24
#define CFG0_I2S_MODE_MASK		0x3
#define CFG0_I2S_MODE_RTJ		0x1
#define CFG0_I2S_MODE_LTJ		0x0
#define CFG0_I2S_MODE_I2S		0x2
#define CFG0_W_LENGTH_MASK		0x30
#define CFG0_W_LENGTH_24BIT		0x00
#define CFG0_W_LENGTH_16BIT		0x10
#define GRL_CFG1		0x28
#define CFG1_EDG_SEL			BIT(0)
#define CFG1_SPDIF			BIT(1)
#define CFG1_DVI			BIT(2)
#define CFG1_HDCP_DEBUG			BIT(3)
#define GRL_CFG2		0x2c
#define CFG2_MHL_DE_SEL			BIT(3)
#define CFG2_MHL_FAKE_DE_SEL		BIT(4)
#define CFG2_MHL_DATA_REMAP		BIT(5)
#define CFG2_NOTICE_EN			BIT(6)
#define CFG2_ACLK_INV			BIT(7)
#define GRL_CFG3		0x30
#define CFG3_AES_KEY_INDEX_MASK		0x3f
#define CFG3_CONTROL_PACKET_DELAY	BIT(6)
#define CFG3_KSV_LOAD_START		BIT(7)
#define GRL_CFG4		0x34
#define CFG4_AES_KEY_LOAD		BIT(4)
#define CFG4_AV_UNMUTE_EN		BIT(5)
#define CFG4_AV_UNMUTE_SET		BIT(6)
#define CFG4_MHL_MODE			BIT(7)
#define GRL_CFG5		0x38
#define CFG5_CD_RATIO_MASK	0x8F
#define CFG5_FS128			(0x1 << 4)
#define CFG5_FS256			(0x2 << 4)
#define CFG5_FS384			(0x3 << 4)
#define CFG5_FS512			(0x4 << 4)
#define CFG5_FS768			(0x6 << 4)
#define DUMMY_304		0x304
#define CHMO_SEL			(0x3 << 2)
#define CHM1_SEL			(0x3 << 4)
#define CHM2_SEL			(0x3 << 6)
#define AUDIO_I2S_NCTS_SEL		BIT(1)
#define AUDIO_I2S_NCTS_SEL_64		(1 << 1)
#define AUDIO_I2S_NCTS_SEL_128		(0 << 1)
#define NEW_GCP_CTRL			BIT(0)
#define NEW_GCP_CTRL_MERGE		BIT(0)
#define GRL_L_STATUS_0		0x200
#define GRL_L_STATUS_1		0x204
#define GRL_L_STATUS_2		0x208
#define GRL_L_STATUS_3		0x20c
#define GRL_L_STATUS_4		0x210
#define GRL_L_STATUS_5		0x214
#define GRL_L_STATUS_6		0x218
#define GRL_L_STATUS_7		0x21c
#define GRL_L_STATUS_8		0x220
#define GRL_L_STATUS_9		0x224
#define GRL_L_STATUS_10		0x228
#define GRL_L_STATUS_11		0x22c
#define GRL_L_STATUS_12		0x230
#define GRL_L_STATUS_13		0x234
#define GRL_L_STATUS_14		0x238
#define GRL_L_STATUS_15		0x23c
#define GRL_L_STATUS_16		0x240
#define GRL_L_STATUS_17		0x244
#define GRL_L_STATUS_18		0x248
#define GRL_L_STATUS_19		0x24c
#define GRL_L_STATUS_20		0x250
#define GRL_L_STATUS_21		0x254
#define GRL_L_STATUS_22		0x258
#define GRL_L_STATUS_23		0x25c
#define GRL_R_STATUS_0		0x260
#define GRL_R_STATUS_1		0x264
#define GRL_R_STATUS_2		0x268
#define GRL_R_STATUS_3		0x26c
#define GRL_R_STATUS_4		0x270
#define GRL_R_STATUS_5		0x274
#define GRL_R_STATUS_6		0x278
#define GRL_R_STATUS_7		0x27c
#define GRL_R_STATUS_8		0x280
#define GRL_R_STATUS_9		0x284
#define GRL_R_STATUS_10		0x288
#define GRL_R_STATUS_11		0x28c
#define GRL_R_STATUS_12		0x290
#define GRL_R_STATUS_13		0x294
#define GRL_R_STATUS_14		0x298
#define GRL_R_STATUS_15		0x29c
#define GRL_R_STATUS_16		0x2a0
#define GRL_R_STATUS_17		0x2a4
#define GRL_R_STATUS_18		0x2a8
#define GRL_R_STATUS_19		0x2ac
#define GRL_R_STATUS_20		0x2b0
#define GRL_R_STATUS_21		0x2b4
#define GRL_R_STATUS_22		0x2b8
#define GRL_R_STATUS_23		0x2bc
#define GRL_ABIST_CTRL0		0x2D4
#define GRL_ABIST_CTRL1		0x2D8
#define ABIST_EN			BIT(7)
#define ABIST_DATA_FMT			(0x7 << 0)
#define VIDEO_CFG_0		0x380
#define VIDEO_CFG_1		0x384
#define VIDEO_CFG_2		0x388
#define VIDEO_CFG_3		0x38c
#define VIDEO_CFG_4		0x390
#define VIDEO_SOURCE_SEL		BIT(7)
#define NORMAL_PATH			(1 << 7)
#define GEN_RGB				(0 << 7)

#define HDMI_SYS_CFG1C		0x000
#define HDMI_ON				BIT(0)
#define HDMI_RST			BIT(1)
#define ANLG_ON				BIT(2)
#define CFG10_DVI			BIT(3)
#define HDMI_TST			BIT(3)
#define SYS_KEYMASK1			(0xff << 8)
#define SYS_KEYMASK2			(0xff << 16)
#define AUD_OUTSYNC_EN			BIT(24)
#define AUD_OUTSYNC_PRE_EN		BIT(25)
#define I2CM_ON				BIT(26)
#define E2PROM_TYPE_8BIT		BIT(27)
#define MCM_E2PROM_ON			BIT(28)
#define EXT_E2PROM_ON			BIT(29)
#define HTPLG_PIN_SEL_OFF		BIT(30)
#define AES_EFUSE_ENABLE		BIT(31)
#define HDMI_SYS_CFG20		0x004
#define DEEP_COLOR_MODE_MASK		(3 << 1)
#define COLOR_8BIT_MODE			(0 << 1)
#define COLOR_10BIT_MODE		(1 << 1)
#define COLOR_12BIT_MODE		(2 << 1)
#define COLOR_16BIT_MODE		(3 << 1)
#define DEEP_COLOR_EN			BIT(0)
#define HDMI_AUDIO_TEST_SEL		BIT(8)
#define HDMI2P0_EN			BIT(11)
#define HDMI_OUT_FIFO_EN		BIT(16)
#define HDMI_OUT_FIFO_CLK_INV		BIT(17)
#define MHL_MODE_ON			BIT(28)
#define MHL_PP_MODE			BIT(29)
#define MHL_SYNC_AUTO_EN		BIT(30)
#define HDMI_PCLK_FREE_RUN		BIT(31)

#define MTK_SIP_SET_AUTHORIZED_SECURE_REG 0x82000001
#endif
